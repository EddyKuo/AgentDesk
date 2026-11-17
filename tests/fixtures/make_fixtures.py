"""Regenerates the byte-exact VT fixtures under tests/fixtures/bytes.

Run with: python -I tests/fixtures/make_fixtures.py
Fixtures are committed; regenerate only when intentionally changing them and review the diff.
"""
import pathlib

ESC = b"\x1b"
ST = ESC + b"\\"
BEL = b"\x07"
HERE = pathlib.Path(__file__).resolve().parent / "bytes"


def mixed_sgr_utf8() -> bytes:
    return b"".join([
        ESC + b"]2;fixture title" + BEL,
        ESC + b"[1;31mRED" + ESC + b"[0m plain " + ESC + b"[38;2;10;20;30mrgb" + ESC + b"[0m\r\n",
        "中文寬字 é emoji \U0001F600 end\r\n".encode("utf-8"),
        ESC + b"[3;5Hpos" + ESC + b"[K\r\n",
        b"line\r\nline2" + ESC + b"[2J" + ESC + b"[Hafter clear\r\n",
        ESC + b"[?1049halt screen text" + ESC + b"[?1049l",
        ESC + b"]7501;state=working:app=fixture:progress=40" + ST,
        "tail 測試\r\n".encode("utf-8"),
        ESC + b"]7501;state=blocked:kind=permission:msg=bmVlZCBhcHByb3ZhbA==" + BEL,
    ])


def scrolling_output() -> bytes:
    lines = [f"line {i:03d} 第{i}行\r\n".encode("utf-8") for i in range(60)]
    return b"".join(lines) + ESC + b"[1;32mdone" + ESC + b"[0m"


def status_query_then_da() -> bytes:
    return ESC + b"]7501;?" + ST + ESC + b"[c"


def main() -> None:
    HERE.mkdir(parents=True, exist_ok=True)
    for name, data in {
        "mixed_sgr_utf8.bin": mixed_sgr_utf8(),
        "scrolling_output.bin": scrolling_output(),
        "status_query_then_da.bin": status_query_then_da(),
    }.items():
        (HERE / name).write_bytes(data)
        print(name, len(data))


if __name__ == "__main__":
    main()
