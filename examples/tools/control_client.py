"""Small synchronous client for XADV2's development control endpoint."""

from __future__ import annotations

import json
import socket
import threading
from typing import Any


class ControlError(RuntimeError):
    def __init__(self, code: int, message: str, data: Any = None):
        super().__init__(f"control error {code}: {message}")
        self.code = code
        self.data = data


class ControlClient:
    """JSON-RPC client with notebook-safe request serialization."""

    def __init__(self, host: str = "127.0.0.1", port: int = 8765, timeout: float = 3.0):
        if host not in {"127.0.0.1", "localhost"}:
            raise ValueError("the development control endpoint is loopback-only")
        self.host = host
        self.port = port
        self.timeout = timeout
        self._socket: socket.socket | None = None
        self._stream = None
        self._next_id = 1
        self._lock = threading.Lock()

    def connect(self) -> "ControlClient":
        self.close()
        self._socket = socket.create_connection((self.host, self.port), self.timeout)
        self._stream = self._socket.makefile("rwb")
        return self

    def close(self) -> None:
        if self._stream is not None:
            self._stream.close()
            self._stream = None
        if self._socket is not None:
            self._socket.close()
            self._socket = None

    def __enter__(self) -> "ControlClient":
        return self.connect()

    def __exit__(self, *_exc: object) -> None:
        self.close()

    @property
    def connected(self) -> bool:
        return self._stream is not None

    def call(self, method: str, params: dict[str, Any] | list[Any] | None = None) -> Any:
        with self._lock:
            if self._stream is None:
                self.connect()
            request_id = self._next_id
            self._next_id += 1
            request = {
                "jsonrpc": "2.0",
                "id": request_id,
                "method": method,
                "params": {} if params is None else params,
            }
            assert self._stream is not None
            self._stream.write(json.dumps(request, separators=(",", ":")).encode("utf-8") + b"\n")
            self._stream.flush()
            line = self._stream.readline()
            if not line:
                self.close()
                raise ConnectionError("the engine closed the control connection")
            response = json.loads(line)
            if response.get("id") != request_id:
                raise RuntimeError(f"unexpected JSON-RPC response id: {response.get('id')!r}")
            if "error" in response:
                error = response["error"]
                raise ControlError(error["code"], error["message"], error.get("data"))
            return response.get("result")

    def describe(self) -> dict[str, Any]:
        return self.call("engine.describe")

    def room_render_state(self) -> dict[str, Any]:
        return self.call("room.render.get")

    def lua_call(self, function: str, *args: Any) -> Any:
        return self.call("lua.call", {"function": function, "args": list(args)})

    def lua_eval(self, code: str) -> Any:
        return self.call("lua.eval", {"code": code})

