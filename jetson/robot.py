"""
Robot System 模組：SO-101 Arm + Camera + AI Vision
Phase 1：模擬模式（不需硬體）
Phase 2：真實 AI 視覺（YOLO / PatchCore）
Phase 3：真實 SO-101 手臂（LeRobot）
"""
import asyncio
import logging
import random
import time
from dataclasses import dataclass
from typing import Optional

import config

log = logging.getLogger(__name__)


@dataclass
class InspectionResult:
    passed: bool
    confidence: float
    defects: list[str]       # 缺陷描述列表
    model_used: str
    image_path: Optional[str] = None
    duration_sec: float = 0.0


# ─────────────────────────────────────────────
# 視覺檢測
# ─────────────────────────────────────────────

class VisionInspector:
    """AI 視覺檢測（Phase 2+ 使用真實模型）"""

    def __init__(self):
        if not config.VISION_SIMULATION:
            self._load_model()

    def _load_model(self):
        import torch
        from ultralytics import YOLO
        log.info(f"載入視覺模型：{config.VISION_MODEL_PATH}")
        self.model = YOLO(config.VISION_MODEL_PATH)
        log.info("視覺模型載入完成")

    def capture_image(self) -> Optional[str]:
        """從相機擷取影像，回傳暫存路徑"""
        if config.VISION_SIMULATION:
            log.info("[SIM] 模擬拍照")
            return None

        import cv2
        cap = cv2.VideoCapture(config.CAMERA_ID)
        if not cap.isOpened():
            log.error("無法開啟相機")
            return None
        ret, frame = cap.read()
        cap.release()
        if not ret:
            return None
        path = f"/tmp/inspection_{int(time.time())}.jpg"
        cv2.imwrite(path, frame)
        log.info(f"拍照完成：{path}")
        return path

    def inspect(self, image_path: Optional[str]) -> InspectionResult:
        """執行 AI 視覺檢測"""
        t0 = time.time()

        if config.VISION_SIMULATION:
            # Phase 1：隨機模擬結果（用於系統整合測試）
            passed = random.random() > 0.3
            return InspectionResult(
                passed=passed,
                confidence=round(random.uniform(0.80, 0.99), 3),
                defects=[] if passed else [random.choice(["表面刮痕", "尺寸偏差", "顏色異常", "缺料"])],
                model_used="simulation",
                image_path=image_path,
                duration_sec=round(time.time() - t0, 2),
            )

        # Phase 2+：真實 YOLO 推論
        results = self.model(image_path, conf=config.VISION_THRESHOLD)
        defects = []
        for r in results:
            for box in r.boxes:
                cls_name = self.model.names[int(box.cls[0])]
                if cls_name != "ok":
                    defects.append(f"{cls_name} (conf={float(box.conf[0]):.2f})")

        passed = len(defects) == 0
        confidence = float(results[0].boxes.conf.max()) if results[0].boxes else 0.5
        return InspectionResult(
            passed=passed,
            confidence=round(confidence, 3),
            defects=defects,
            model_used=config.VISION_MODEL_PATH,
            image_path=image_path,
            duration_sec=round(time.time() - t0, 2),
        )


# ─────────────────────────────────────────────
# SO-101 手臂控制
# ─────────────────────────────────────────────

class RobotArm:
    """SO-101 手臂（Phase 3+ 使用 LeRobot 真實控制）"""

    def __init__(self):
        if not config.ROBOT_SIMULATION:
            self._connect()

    def _connect(self):
        # LeRobot SO-101 初始化
        # from lerobot.common.robot_devices.robots.factory import make_robot
        # self.robot = make_robot("so101", port=config.ROBOT_PORT)
        # self.robot.connect()
        log.info("SO-101 手臂已連線")

    async def grab_product(self):
        """抓取待測產品"""
        if config.ROBOT_SIMULATION:
            log.info("[SIM] 抓取產品")
            await asyncio.sleep(1.5)
            return
        # self.robot.move_to("pick_position")
        # self.robot.grip_close()

    async def move_to_inspection(self):
        """移動到檢測區"""
        if config.ROBOT_SIMULATION:
            log.info("[SIM] 移動到檢測區")
            await asyncio.sleep(1.5)
            return
        # self.robot.move_to("inspection_position")

    async def place_product(self, passed: bool):
        """依結果放入 OK / NG 箱"""
        target = config.OK_BIN_POSITION if passed else config.NG_BIN_POSITION
        if config.ROBOT_SIMULATION:
            log.info(f"[SIM] 放置產品 → {target}")
            await asyncio.sleep(1.5)
            return
        # self.robot.move_to(target)
        # self.robot.grip_open()

    async def home(self):
        """回到初始位置"""
        if config.ROBOT_SIMULATION:
            log.info("[SIM] 手臂回到 Home")
            await asyncio.sleep(1.0)
            return
        # self.robot.move_to("home")


# ─────────────────────────────────────────────
# 整合：完整檢測流程
# ─────────────────────────────────────────────

class RobotSystem:
    """
    完整機器人品管流程：
    抓取 → 移到檢測區 → 拍照 → AI 判斷 → 分類放置 → 回傳結果
    """

    def __init__(self):
        self.arm = RobotArm()
        self.vision = VisionInspector()

    async def run_inspection(self, spec_context: str = "") -> InspectionResult:
        """執行一次完整品管流程，回傳檢測結果"""
        log.info("=== 開始品管流程 ===")

        # 1. 抓取產品
        await self.arm.grab_product()

        # 2. 移動到檢測區
        await self.arm.move_to_inspection()

        # 3. 拍照
        image_path = self.vision.capture_image()

        # 4. AI 視覺檢測
        result = self.vision.inspect(image_path)
        log.info(f"檢測結果：{'合格' if result.passed else '不合格'}，信心度={result.confidence:.1%}")

        # 5. 依結果分類放置
        await self.arm.place_product(result.passed)

        # 6. 回到初始位置
        await self.arm.home()

        log.info("=== 品管流程完成 ===")
        return result
