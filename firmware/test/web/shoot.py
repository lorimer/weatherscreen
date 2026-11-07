#!/usr/bin/env python3
"""Drives the web page in headless Chrome against mock_server.py and saves screenshots."""
import sys, time
from playwright.sync_api import sync_playwright
URL = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8765/"
OUT = sys.argv[2] if len(sys.argv) > 2 else "out"
import os; os.makedirs(OUT, exist_ok=True)
errors = []
with sync_playwright() as p:
    b = p.chromium.launch(executable_path="/usr/bin/google-chrome", args=["--no-sandbox"])
    pg = b.new_page(viewport={"width": 1280, "height": 900}, timezone_id="America/Denver")
    pg.on("console", lambda m: errors.append(m.text) if m.type == "error" else None)
    pg.on("pageerror", lambda e: errors.append(str(e)))
    pg.goto(URL); pg.wait_for_timeout(800)
    pg.screenshot(path=f"{OUT}/overview.png")
    def ready():
        pg.wait_for_function("!document.querySelector('#trends').classList.contains('busy')", timeout=180000)
        pg.wait_for_timeout(400)
    pg.click("nav button[data-t=trends]"); pg.wait_for_timeout(300); ready()
    pg.screenshot(path=f"{OUT}/trends_week.png", full_page=True)
    # hover on pressure chart
    box = pg.locator("#cP svg").bounding_box()
    pg.mouse.move(box["x"] + box["width"] * 0.6, box["y"] + box["height"] * 0.5); pg.wait_for_timeout(200)
    pg.screenshot(path=f"{OUT}/trends_week_hover.png", clip={"x": 0, "y": box["y"] - 60, "width": 1280, "height": 560})
    for d, name in (("30", "month"), ("365", "year"), ("all", "all")):
        pg.click(f"#presets button[data-d='{d}']"); pg.wait_for_timeout(300); ready()
        pg.screenshot(path=f"{OUT}/trends_{name}.png", full_page=True)
    # drag-zoom on the year view's pain chart
    pg.click("#presets button[data-d='365']"); pg.wait_for_timeout(300); ready()
    box = pg.locator("#cN svg").bounding_box()
    y = box["y"] + box["height"] / 2
    pg.mouse.move(box["x"] + box["width"] * 0.55, y); pg.mouse.down()
    pg.mouse.move(box["x"] + box["width"] * 0.62, y, steps=8); pg.mouse.up()
    pg.wait_for_timeout(300); ready()
    print("after zoom:", pg.inner_text("#rlabel"))
    pg.screenshot(path=f"{OUT}/trends_zoomed.png", full_page=True)
    pg.mouse.move(box["x"] + box["width"] * 0.5, y); pg.wait_for_timeout(200)
    pg.screenshot(path=f"{OUT}/trends_zoomed_hover.png", clip={"x": 0, "y": pg.locator("#cP svg").bounding_box()["y"] - 60, "width": 1280, "height": 620})
    b.close()
print("console errors:", errors or "none")
