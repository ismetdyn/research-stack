from __future__ import annotations

from typing import Any
from pydantic import BaseModel, Field


class WorkCandidate(BaseModel):
    source: str
    source_id: str | None = None

    title: str
    doi: str | None = None
    year: int | None = None
    abstract: str | None = None

    authors: list[str] = Field(default_factory=list)

    url: str | None = None
    open_access_url: str | None = None

    is_open_access: bool | None = None
    citation_count: int | None = None

    work_type: str | None = None

    raw: dict[str, Any] = Field(
        default_factory=dict,
        exclude=True,
    )


class MergedWork(BaseModel):
    title: str
    doi: str | None = None
    year: int | None = None
    abstract: str | None = None

    authors: list[str] = Field(default_factory=list)

    urls: list[str] = Field(default_factory=list)
    open_access_urls: list[str] = Field(default_factory=list)

    citation_count: int | None = None
    is_open_access: bool | None = None

    sources: list[str] = Field(default_factory=list)
    source_ids: dict[str, str] = Field(default_factory=dict)
