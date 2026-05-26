"""
OpenClaw Agent
負責：意圖解析 → RAG Spec 查詢 → 任務派送 → 報告生成
使用 LangChain Tool Calling Agent（支援 Ollama 本地 / OpenAI 雲端）
"""
import asyncio
import logging
from typing import Optional

from langchain.agents import AgentExecutor, create_tool_calling_agent
from langchain.tools import tool
from langchain_core.prompts import ChatPromptTemplate, MessagesPlaceholder

import config
from rag import SpecRetriever
from robot import RobotSystem, InspectionResult
from report import generate_report, make_tts_summary

log = logging.getLogger(__name__)


def _build_llm():
    """根據 config 建立 LLM 實例"""
    if config.LLM_PROVIDER == "ollama":
        from langchain_ollama import ChatOllama
        return ChatOllama(
            model=config.LLM_MODEL,
            base_url=config.OLLAMA_BASE_URL,
            temperature=0,
        )
    else:
        from langchain_openai import ChatOpenAI
        return ChatOpenAI(
            model=config.OPENAI_MODEL,
            api_key=config.OPENAI_API_KEY,
            temperature=0,
        )


class OpenClawAgent:
    """
    工廠品管 AI Agent。
    每個 OpenClawAgent 實例持有 RAG 和 Robot 的引用，
    工具透過閉包存取這些引用（LangChain @tool 限制）。
    """

    def __init__(self):
        log.info("初始化 OpenClaw Agent...")
        self.retriever = SpecRetriever(config.VECTOR_STORE_PATH, config.SPEC_DOCS_PATH)
        self.robot     = RobotSystem()
        self.llm       = _build_llm()
        self._last_result: Optional[InspectionResult] = None
        self._last_spec:   str = ""

        # 建立工具（閉包捕捉 self）
        tools = self._build_tools()

        prompt = ChatPromptTemplate.from_messages([
            ("system", (
                "你是一個工廠品管 AI 助理（OpenClaw Agent）。\n"
                "你負責：解讀使用者指令、查詢產品規格、派送檢測任務、生成報告。\n"
                "回應請用繁體中文，簡潔口語化，適合語音播報。\n"
                "若需要查 Spec 再決定如何檢測，請先呼叫 query_spec。\n"
                "若使用者要求開始檢測，請呼叫 run_inspection。\n"
                "最後請呼叫 generate_report 產出報告。"
            )),
            MessagesPlaceholder("chat_history", optional=True),
            ("human", "{input}"),
            MessagesPlaceholder("agent_scratchpad"),
        ])

        agent = create_tool_calling_agent(self.llm, tools, prompt)
        self.executor = AgentExecutor(
            agent=agent,
            tools=tools,
            verbose=True,
            max_iterations=8,
            handle_parsing_errors=True,
        )
        log.info("OpenClaw Agent 初始化完成")

    def _build_tools(self):
        retriever = self.retriever
        robot     = self.robot
        agent_ref = self

        @tool
        def query_spec(query: str) -> str:
            """
            查詢產品規格（Spec）。
            輸入：查詢問題，例如「產品A的尺寸公差是多少」。
            輸出：從 Spec 文件中找到的相關內容。
            """
            log.info(f"[Tool] query_spec: {query}")
            result = retriever.query(query)
            agent_ref._last_spec = result
            return result

        @tool
        def run_inspection(spec_context: str = "") -> str:
            """
            派送品管任務給機器人系統（SO-101 手臂 + 視覺 AI）。
            輸入：相關規格描述（可選，從 query_spec 取得）。
            輸出：檢測結果摘要。
            """
            log.info("[Tool] run_inspection")
            # 在同步 tool 內執行 async 函數
            loop = asyncio.new_event_loop()
            result = loop.run_until_complete(robot.run_inspection(spec_context))
            loop.close()
            agent_ref._last_result = result
            verdict = "合格" if result.passed else "不合格"
            defects = "、".join(result.defects) if result.defects else "無"
            return (
                f"檢測完成：{verdict}，信心度={result.confidence:.1%}，"
                f"缺陷={defects}，耗時={result.duration_sec:.1f}秒"
            )

        @tool
        def generate_report_tool(command: str) -> str:
            """
            生成並儲存檢查報告。
            輸入：原始使用者指令（用於記錄）。
            輸出：確認報告已儲存。
            """
            log.info("[Tool] generate_report")
            if agent_ref._last_result is None:
                return "尚未執行檢測，請先呼叫 run_inspection"
            report = generate_report(command, agent_ref._last_spec, agent_ref._last_result)
            return f"報告已儲存，Task ID：{report['task_id']}"

        return [query_spec, run_inspection, generate_report_tool]

    async def handle(self, user_text: str) -> str:
        """
        處理一條語音指令，回傳適合 TTS 播報的結果文字。
        """
        log.info(f"OpenClaw Agent 收到指令：{user_text}")
        self._last_result = None
        self._last_spec   = ""

        try:
            # Agent 推論（同步呼叫，在 thread pool 中執行）
            loop = asyncio.get_event_loop()
            response = await loop.run_in_executor(
                None,
                lambda: self.executor.invoke({"input": user_text})
            )
            agent_output = response.get("output", "")
            log.info(f"Agent 輸出：{agent_output}")

            # 如果有真實的機器人執行結果，優先用結構化 TTS 摘要
            if self._last_result is not None:
                return make_tts_summary(self._last_result)

            # 否則直接播報 Agent 的文字回應
            return agent_output if agent_output else "指令已處理完成"

        except Exception as e:
            log.error(f"Agent 執行錯誤：{e}", exc_info=True)
            return "很抱歉，執行時發生錯誤，請再試一次"
