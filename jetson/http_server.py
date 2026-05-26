"""
HTTP Server — ESP32 音訊上傳 + LINE Webhook
POST /process      : 接收 ESP32 WAV 音訊 → Whisper ASR → 非同步 Agent
POST /line/webhook : 接收 LINE 文字/語音 → 非同步 Agent
"""
import asyncio
import base64
import hashlib
import hmac
import json
import logging
import os
import tempfile

import httpx
from fastapi import BackgroundTasks, FastAPI, File, HTTPException, Request, UploadFile
from fastapi.responses import JSONResponse

import config

log = logging.getLogger(__name__)

app = FastAPI(title="OpenClaw HTTP Server")

_agent = None
_whisper_model = None


def set_agent(agent):
    global _agent
    _agent = agent


def _load_whisper():
    global _whisper_model
    if _whisper_model is None:
        from faster_whisper import WhisperModel
        log.info(f"載入 Whisper 模型：{config.WHISPER_MODEL} / {config.WHISPER_DEVICE} / {config.WHISPER_COMPUTE_TYPE}")
        _whisper_model = WhisperModel(
            config.WHISPER_MODEL,
            device=config.WHISPER_DEVICE,
            compute_type=config.WHISPER_COMPUTE_TYPE,
        )
        log.info("Whisper 模型載入完成")
    return _whisper_model


async def _transcribe(audio_bytes: bytes) -> str:
    """在 thread executor 執行 faster-whisper（避免阻塞 event loop）"""
    loop = asyncio.get_event_loop()

    def _run():
        model = _load_whisper()
        with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
            f.write(audio_bytes)
            tmp_path = f.name
        try:
            segments, info = model.transcribe(
                tmp_path, language="zh", beam_size=5
            )
            text = "".join(seg.text for seg in segments).strip()
            log.info(f"[ASR] lang={info.language} text={text!r}")
            return text
        finally:
            os.unlink(tmp_path)

    return await loop.run_in_executor(None, _run)


async def _run_agent(text: str):
    try:
        log.info(f"[Agent] 開始處理：{text!r}")
        await _agent.handle(text)
    except Exception as e:
        log.error(f"[Agent] 錯誤：{e}", exc_info=True)


# ── POST /process ─────────────────────────────────────────────────────────────
@app.post("/process")
async def process_audio(
    background_tasks: BackgroundTasks,
    file: UploadFile = File(...),
):
    """
    接收 ESP32 WAV 音訊（multipart/form-data, field name='file'）。
    完成 ASR 後立即回傳 {"status": "ok", "text": "..."}，
    Agent pipeline（LLM → RAG → Robot → LINE）在背景非同步執行。
    """
    if _agent is None:
        raise HTTPException(503, detail="Agent not ready")

    audio_bytes = await file.read()
    log.info(f"[/process] 收到音訊 {len(audio_bytes)} bytes，filename={file.filename!r}")

    text = await _transcribe(audio_bytes)

    if text:
        background_tasks.add_task(_run_agent, text)
    else:
        log.warning("[/process] ASR 結果為空，略過 Agent")

    return JSONResponse({"status": "ok", "text": text})


# ── POST /line/webhook ────────────────────────────────────────────────────────
@app.post("/line/webhook")
async def line_webhook(request: Request, background_tasks: BackgroundTasks):
    """
    接收 LINE Messaging API Webhook。
    文字訊息：直接送 Agent。
    語音訊息：下載音訊 → ASR → Agent。
    """
    body = await request.body()

    # 驗證 LINE 簽名
    if config.LINE_CHANNEL_SECRET:
        sig = request.headers.get("X-Line-Signature", "")
        mac = hmac.new(
            config.LINE_CHANNEL_SECRET.encode("utf-8"),
            body,
            hashlib.sha256,
        )
        expected = base64.b64encode(mac.digest()).decode()
        if not hmac.compare_digest(sig, expected):
            raise HTTPException(400, detail="invalid signature")

    payload = json.loads(body)
    for event in payload.get("events", []):
        msg = event.get("message", {})
        msg_type = msg.get("type")

        if msg_type == "text":
            text = msg.get("text", "").strip()
            if text:
                log.info(f"[LINE] 文字訊息：{text!r}")
                background_tasks.add_task(_run_agent, text)

        elif msg_type == "audio":
            log.info(f"[LINE] 語音訊息 id={msg.get('id')}")
            background_tasks.add_task(_handle_line_audio, msg.get("id"))

    return JSONResponse({"status": "ok"})


async def _handle_line_audio(message_id: str):
    if not message_id:
        return
    try:
        url = f"https://api-data.line.me/v2/bot/message/{message_id}/content"
        headers = {"Authorization": f"Bearer {config.LINE_CHANNEL_ACCESS_TOKEN}"}
        async with httpx.AsyncClient(timeout=30) as client:
            resp = await client.get(url, headers=headers)
            resp.raise_for_status()
        text = await _transcribe(resp.content)
        if text:
            await _run_agent(text)
    except Exception as e:
        log.error(f"[LINE audio] 下載/ASR 錯誤：{e}", exc_info=True)
