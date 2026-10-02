import re
import unicodedata
from typing import Annotated, Literal

from pydantic import AfterValidator, BaseModel, ConfigDict, Field, field_validator

MAX_ID = 281474976710655


def valid_id(value):
    if not re.fullmatch(r"[1-9][0-9]{0,14}", value) or int(value) > MAX_ID:
        raise ValueError("Use a positive bare Telegram ID")
    return value


def valid_label(value):
    value = value.strip()
    if not value or any(unicodedata.category(c).startswith("C") for c in value):
        raise ValueError("Use a nonempty label without control characters")
    return value


Id = Annotated[str, Field(strict=True, max_length=15), AfterValidator(valid_id)]
Label = Annotated[
    str, Field(strict=True, min_length=1, max_length=128), AfterValidator(valid_label)
]


class Model(BaseModel):
    model_config = ConfigDict(extra="forbid", strict=True)


class Identity(Model):
    id: Id
    label: Label


class Peer(Model):
    kind: Literal["user", "chat", "channel"]
    id: Id
    label: Label | None = None


class PeerList(Model):
    peers: list[Peer] = Field(max_length=10000)

    @field_validator("peers")
    @classmethod
    def unique(cls, peers):
        keys = [(p.kind, p.id) for p in peers]
        if len(keys) != len(set(keys)):
            raise ValueError("Duplicate peer")
        return sorted(peers, key=lambda p: (p.kind, p.id))


class PolicyEdit(PeerList):
    expected_revision: int = Field(ge=0, le=9007199254740990)


class Assign(Model):
    head_ids: list[Id] = Field(max_length=100)


class HeadEdit(Model):
    label: Label
    active: bool
