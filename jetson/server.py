"""
Jetson Orin 伺服器 — 入口點
同時啟動：
  - HTTP  (port 8080)：POST /process（ESP32 WAV）、POST /line/webhook
  - WebSocket (port 8765)：ESP32 文字指令（保留供未來使用）

啟動方式：
    python3 server.py
"""
import asyncio
import json
import logging
import sys

import uvicorn
import websockets

import config
import http_server
from agent import OpenClawAgent

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    handlers=[
        logging.StreamHandler(sys.stdout),
        logging.FileHandler("jetson_server.log", encoding="utf-8"),
    ],
)
log = logging.getLogger("server")


# ── 全域 Agent 實例（啟動時初始化一次）──────────────
agent: OpenClawAgent = None


async def send_response(websocket, text: str):
    """回傳結果給 ESP32（JSON 格式，type=response）"""
    msg = json.dumps({"type": "response", "text": text}, ensure_ascii=False)
    await websocket.send(msg)
    log.info(f"→ ESP32: {text[:80]}...")


async def handle_client(websocket, path):
    addr = websocket.remote_address
    log.info(f"ESP32 已連線：{addr}")
    try:
        async for raw in websocket:
            try:
                data = json.loads(raw)
            except json.JSONDecodeError:
                log.warning(f"收到非法 JSON：{raw[:50]}")
                continue

            msg_type = data.get("type", "")
            text     = data.get("text", "").strip()

            if msg_type == "command" and text:
                log.info(f"← ESP32 指令：{text}")
                # 立即回應「收到」，避免 ESP32 逾時
                await send_response(websocket, "收到指令，正在處理中，請稍候...")
                # 執行 Agent 流程
                reply = await agent.handle(text)
                await send_response(websocket, reply)

            elif msg_type == "ping":
                await websocket.send(json.dumps({"type": "pong"}))

            else:
                log.warning(f"未知訊息類型：{msg_type}")

    except websockets.exceptions.ConnectionClosed:
        log.info(f"ESP32 斷線：{addr}")
    except Exception as e:
        log.error(f"連線處理錯誤：{e}", exc_info=True)
        try:
            await send_response(websocket, "系統錯誤，請重新嘗試")
        except Exception:
            pass


async def main():
    global agent

    log.info("═" * 50)
    log.info("  工廠品管系統 — Jetson Orin 伺服器")
    log.info(f"  Phase {config.PHASE}  |  LLM={config.LLM_MODEL}")
    log.info(f"  Robot={'模擬' if config.ROBOT_SIMULATION else '真實'}  |  Vision={'模擬' if config.VISION_SIMULATION else '真實'}")
    log.info("═" * 50)

    log.info("初始化 OpenClaw Agent（首次啟動會載入 Embedding 模型...）")
    agent = OpenClawAgent()
    http_server.set_agent(agent)

    # HTTP server（uvicorn）使用現有 event loop
    http_cfg = uvicorn.Config(
        http_server.app,
        host=config.HTTP_HOST,
        port=config.HTTP_PORT,
        log_level="info",
        loop="none",  # 沿用外層 asyncio event loop
    )
    http_srv = uvicorn.Server(http_cfg)

    log.info(f"HTTP  伺服器：http://{config.HTTP_HOST}:{config.HTTP_PORT}")
    log.info(f"WebSocket 伺服器：ws://{config.WS_HOST}:{config.WS_PORT}")

    async with websockets.serve(handle_client, config.WS_HOST, config.WS_PORT):
        await http_srv.serve()  # 永久運行，直到收到 SIGINT/SIGTERM


if __name__ == "__main__":
    asyncio.run(main())
