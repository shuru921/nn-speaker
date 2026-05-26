"""
RAG Spec 檢索模組
支援：PDF / Excel / CSV / JSON
Embedding：bge-m3（中英混合）
Vector DB：FAISS
"""
import os
import logging
from pathlib import Path
from typing import Optional

log = logging.getLogger(__name__)


def _load_documents(docs_path: str) -> list:
    """載入 specs/ 資料夾下所有支援格式的文件"""
    from langchain_community.document_loaders import (
        PyPDFLoader,
        CSVLoader,
        JSONLoader,
        UnstructuredExcelLoader,
    )
    from langchain.text_splitter import RecursiveCharacterTextSplitter

    docs = []
    path = Path(docs_path)
    if not path.exists():
        log.warning(f"Spec 資料夾不存在：{docs_path}，請建立並放入文件")
        return docs

    loaders = {
        ".pdf":  lambda p: PyPDFLoader(str(p)),
        ".csv":  lambda p: CSVLoader(str(p)),
        ".json": lambda p: JSONLoader(str(p), jq_schema=".", text_content=False),
        ".xlsx": lambda p: UnstructuredExcelLoader(str(p)),
        ".xls":  lambda p: UnstructuredExcelLoader(str(p)),
    }

    for file in path.rglob("*"):
        if file.suffix.lower() in loaders:
            try:
                loader = loaders[file.suffix.lower()](file)
                docs.extend(loader.load())
                log.info(f"載入文件：{file.name}")
            except Exception as e:
                log.error(f"載入 {file.name} 失敗：{e}")

    if not docs:
        log.warning("沒有找到任何 Spec 文件")
        return docs

    splitter = RecursiveCharacterTextSplitter(
        chunk_size=512,
        chunk_overlap=64,
        separators=["\n\n", "\n", "。", "，", " "],
    )
    return splitter.split_documents(docs)


def build_vector_store(docs_path: str, store_path: str):
    """建立（或重建）向量資料庫，存到磁碟"""
    from langchain_community.vectorstores import FAISS
    from langchain_community.embeddings import HuggingFaceEmbeddings

    log.info("建立 RAG 向量資料庫...")
    chunks = _load_documents(docs_path)
    if not chunks:
        raise ValueError("無法建立 Vector Store：沒有文件")

    embeddings = HuggingFaceEmbeddings(
        model_name="BAAI/bge-m3",
        model_kwargs={"device": "cuda"},   # Jetson 用 CUDA
        encode_kwargs={"normalize_embeddings": True},
    )
    store = FAISS.from_documents(chunks, embeddings)
    store.save_local(store_path)
    log.info(f"Vector Store 儲存至 {store_path}（共 {len(chunks)} chunks）")
    return store


class SpecRetriever:
    """RAG Spec 查詢介面"""

    def __init__(self, store_path: str, docs_path: str):
        from langchain_community.vectorstores import FAISS
        from langchain_community.embeddings import HuggingFaceEmbeddings

        self.embeddings = HuggingFaceEmbeddings(
            model_name="BAAI/bge-m3",
            model_kwargs={"device": "cuda"},
            encode_kwargs={"normalize_embeddings": True},
        )

        if os.path.exists(store_path):
            log.info("載入已存在的 Vector Store")
            self.store = FAISS.load_local(
                store_path, self.embeddings, allow_dangerous_deserialization=True
            )
        else:
            log.info("Vector Store 不存在，重新建立")
            self.store = build_vector_store(docs_path, store_path)

    def query(self, question: str, top_k: int = 4) -> str:
        """檢索最相關的 Spec 片段，回傳合併文字"""
        docs = self.store.similarity_search(question, k=top_k)
        if not docs:
            return "未找到相關 Spec"
        return "\n\n---\n\n".join(d.page_content for d in docs)

    def query_with_scores(self, question: str, top_k: int = 4) -> list[dict]:
        """回傳帶相似度分數的結果"""
        results = self.store.similarity_search_with_score(question, k=top_k)
        return [
            {"content": doc.page_content, "score": float(score), "source": doc.metadata.get("source", "unknown")}
            for doc, score in results
        ]
