"""
系統設定 — 修改 PHASE 切換開發階段：
  Phase 1：MVP，Robot 與 Vision 使用模擬模式
  Phase 2：加入 AI 視覺（真實模型）
  Phase 3：加入 SO-101 手臂（真實控制）
  Phase 4：生產優化
"""
import os

# ── 開發階段 ────────────────────────────────
PHASE = 1  # 改為 2 / 3 / 4 以啟用更多硬體

# ── LLM 設定 ────────────────────────────────
# 本地 Ollama（推薦 Qwen2.5 / Llama3）
LLM_PROVIDER   = "ollama"           # "ollama" | "openai"
LLM_MODEL      = "qwen2.5:7b"       # Ollama model name
OLLAMA_BASE_URL = "http://localhost:11434"

# 若使用 OpenAI API
OPENAI_API_KEY  = os.getenv("OPENAI_API_KEY", "")
OPENAI_MODEL    = "gpt-4o-mini"

# ── RAG 設定 ────────────────────────────────
EMBEDDING_MODEL    = "BAAI/bge-m3"       # 中英混合效果最好
VECTOR_STORE_PATH  = "./data/vector_store"
SPEC_DOCS_PATH     = "./data/specs"       # 放 PDF / Excel / CSV / JSON

# ── Robot 設定 ────────────────────────────────
ROBOT_SIMULATION   = PHASE < 3           # Phase 3+ 才啟用真實手臂
ROBOT_PORT         = "/dev/ttyUSB0"      # SO-101 序列埠
OK_BIN_POSITION    = "bin_ok"            # LeRobot 預定義位置名稱
NG_BIN_POSITION    = "bin_ng"

# ── Vision 設定 ────────────────────────────────
VISION_SIMULATION  = PHASE < 2           # Phase 2+ 才啟用真實模型
CAMERA_ID          = 0
VISION_MODEL_PATH  = "./models/yolov8_inspection.pt"
VISION_THRESHOLD   = 0.7                 # 信心度門檻

# ── 報告設定 ────────────────────────────────
REPORT_OUTPUT_DIR  = "./reports"

# ── WebSocket 設定 ────────────────────────────────
WS_HOST = "0.0.0.0"
WS_PORT = 8765

# ── HTTP Server 設定（ESP32 音訊上傳 + LINE Webhook）──
HTTP_HOST = "0.0.0.0"
HTTP_PORT = 8080

# ── faster-whisper 設定 ────────────────────────────
# 模型大小：tiny / base / small / medium / large-v3
WHISPER_MODEL        = "small"
WHISPER_DEVICE       = "cuda"    # Jetson 用 cuda；無 GPU 改 cpu
WHISPER_COMPUTE_TYPE = "float16" # cuda→float16；cpu→int8

# ── LINE Messaging API ─────────────────────────────
LINE_CHANNEL_SECRET       = os.getenv("LINE_CHANNEL_SECRET", "")
LINE_CHANNEL_ACCESS_TOKEN = os.getenv("LINE_CHANNEL_ACCESS_TOKEN", "")
