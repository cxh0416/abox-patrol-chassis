"""Include paths shared by host tests for the product's functional modules."""
from pathlib import Path

USER = Path(__file__).resolve().parents[1] / "app/Core/User"
MODULES = ("system", "motion", "cloud", "mqtt", "ota", "board", "common")

def include_args(*, private=()):
    return ["-I" + str(USER / module / "include") for module in MODULES] + [
        "-I" + str(USER / module / "private") for module in private]
