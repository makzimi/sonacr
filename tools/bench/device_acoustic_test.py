#!/usr/bin/env python3
"""Acoustic device test: plays track excerpts on the host speakers and reads the phone's screen over adb.

Method: for each library track and each start position, the demo app is force-restarted, ffplay plays a clip,
and the screen is polled with `adb uiautomator dump` until a "Now playing" card appears. A final trial plays a
long negative file and records any false callback. `detectedWithinMs` is wall-clock from playback start to the
result being seen and includes polling latency (about 1-2 s per uiautomator dump), so it is an upper bound.
Results are appended as JSON lines to the log file.
"""
import argparse
import json
import re
import subprocess
import sys
import time

APP = "com.localacr.demo/.MainActivity"
SERIAL = TRACKS = MANIFEST = POSITIONS = PLAY_SECONDS = LOG = None


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description="Acoustic device recognition test driven over adb.")
    parser.add_argument("log", help="path of the JSON-lines result log (appended)")
    parser.add_argument("--serial", required=True, help="adb device serial")
    parser.add_argument("--tracks-dir", default="local-tracks", help="directory with manifest.json, tracks and negative.wav")
    parser.add_argument("--positions", default="30,90,150", help="comma-separated clip start positions in seconds")
    parser.add_argument("--play-seconds", type=int, default=10, help="clip length in seconds")
    parser.add_argument("--negative-seconds", type=int, default=600, help="negative (unrelated audio) duration in seconds")
    return parser.parse_args(argv)


def adb(*args, timeout=30):
    return subprocess.run(["adb", "-s", SERIAL, *args], capture_output=True, text=True, timeout=timeout).stdout


def screen_texts():
    adb("shell", "uiautomator", "dump", "/sdcard/ui.xml")
    return re.findall(r'text="([^"]+)"', adb("shell", "cat", "/sdcard/ui.xml"))


def restart_app():
    adb("shell", "am", "force-stop", "com.localacr.demo")
    adb("shell", "am", "start", "-n", APP)
    time.sleep(3)
    texts = screen_texts()
    if "Status: Listening" not in texts:
        raise RuntimeError(f"app not listening after restart: {texts}")


def now_playing(texts):
    if "Now playing" not in texts:
        return None, None
    index = texts.index("Now playing")
    title = texts[index + 1] if index + 1 < len(texts) else None
    detail = next((t for t in texts if t.startswith("at ")), None)
    return title, detail


def log(record):
    print(json.dumps(record), flush=True)
    with open(LOG, "a") as out:
        out.write(json.dumps(record) + "\n")


def positive_trials():
    for trigger in MANIFEST["triggers"]:
        for position in POSITIONS:
            restart_app()
            player = subprocess.Popen(["ffplay", "-nodisp", "-autoexit", "-loglevel", "error",
                                       "-ss", str(position), "-t", str(PLAY_SECONDS), f"{TRACKS}/{trigger['audio']}"])
            started = time.monotonic()
            title = detail = None
            while player.poll() is None:
                title, detail = now_playing(screen_texts())
                if title is not None:
                    break
            elapsed_ms = int((time.monotonic() - started) * 1000)
            player.wait()
            log({"kind": "positive", "expected": trigger["displayName"], "positionS": position,
                 "recognized": title, "correct": title == trigger["displayName"],
                 "detectedWithinMs": elapsed_ms if title else None, "card": detail})


def negative_trial(seconds):
    restart_app()
    player = subprocess.Popen(["ffplay", "-nodisp", "-autoexit", "-loglevel", "error",
                               "-t", str(seconds), f"{TRACKS}/negative.wav"])
    false_titles = []
    while player.poll() is None:
        title, detail = now_playing(screen_texts())
        if title is not None:
            false_titles.append({"title": title, "card": detail})
            break
        time.sleep(3)
    player.terminate()
    log({"kind": "negative", "seconds": seconds, "falseCallbacks": len(false_titles), "details": false_titles})


if __name__ == "__main__":
    args = parse_args()
    SERIAL = args.serial
    TRACKS = args.tracks_dir
    MANIFEST = json.load(open(f"{TRACKS}/manifest.json"))
    POSITIONS = [int(p) for p in args.positions.split(",")]
    PLAY_SECONDS = args.play_seconds
    LOG = args.log
    positive_trials()
    negative_trial(args.negative_seconds)
    adb("shell", "am", "force-stop", "com.localacr.demo")
