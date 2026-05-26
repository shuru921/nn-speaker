"""
報告生成模組
- 生成 JSON 結構化報告（存檔）
- 生成 TTS 語音播報文字（回傳 ESP32）
"""
import json
import logging
import os
from datetime import datetime
from typing import Optional

from robot import InspectionResult
import config

log = logging.getLogger(__name__)


def generate_report(
    command: str,
    spec_context: str,
    result: InspectionResult,
    task_id: Optional[str] = None,
) -> dict:
    """生成結構化 JSON 報告"""
    task_id = task_id or datetime.now().strftime("%Y%m%d_%H%M%S")
    report = {
        "task_id": task_id,
        "timestamp": datetime.now().isoformat(),
        "command": command,
        "spec_used": spec_context[:300] + "..." if len(spec_context) > 300 else spec_context,
        "result": {
            "passed": result.passed,
            "confidence": result.confidence,
            "defects": result.defects,
            "model_used": result.model_used,
            "image_path": result.image_path,
            "duration_sec": result.duration_sec,
        },
        "verdict": "PASS" if result.passed else "FAIL",
    }

    # 儲存報告
    os.makedirs(config.REPORT_OUTPUT_DIR, exist_ok=True)
    report_path = os.path.join(config.REPORT_OUTPUT_DIR, f"report_{task_id}.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, ensure_ascii=False, indent=2)
    log.info(f"報告已儲存：{report_path}")

    return report


def make_tts_summary(result: InspectionResult) -> str:
    """
    將檢測結果轉為 TTS 播報文字（簡短、口語化）
    這段文字會透過 WebSocket 回傳給 ESP32，由 OpenAI TTS 播報給使用者
    """
    if result.passed:
        return (
            f"產品檢測完成，結果為合格，信心度 {result.confidence:.0%}，"
            f"已放入合格品箱，報告已儲存。"
        )
    else:
        defect_str = "、".join(result.defects) if result.defects else "未知缺陷"
        return (
            f"產品檢測完成，結果為不合格，"
            f"發現缺陷：{defect_str}，"
            f"已放入不合格品箱，請查看詳細報告。"
        )
