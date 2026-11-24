#!/usr/bin/env python3
"""Captures the current top LLMs for SketchyBar's Claude tab ("Top models").

Usage: claude_models.py <output.json>

1. Reads OpenRouter's public rankings page (https://openrouter.ai/rankings,
   models ranked by weekly token usage) and keeps the model links in the
   order they appear. Only links that are real model ids are kept, so other
   links on the page are ignored.
2. Fills in each model's details from OpenRouter's public model list
   (https://openrouter.ai/api/v1/models): provider, context length, max
   output, price, features.
3. If the rankings page can't be read or parsed, falls back to the model
   list's own "most popular" order.

Writes {"source", "updated", "models": [...]} to <output.json> (atomically).
On failure it leaves the old file alone and exits with status 1. Needs only
the Python standard library; no API key.
"""

import json
import os
import re
import sys
import tempfile
import time
import urllib.request

RANKINGS_URL = "https://openrouter.ai/rankings"
MODELS_URL = "https://openrouter.ai/api/v1/models"
POPULAR_URL = MODELS_URL + "?sort=most-popular"
TOP = 10
TIMEOUT = 20
USER_AGENT = "sketchybar-claude-models/1.0"

PROVIDERS = {
    "anthropic": "Anthropic", "openai": "OpenAI", "google": "Google",
    "x-ai": "xAI", "meta-llama": "Meta", "deepseek": "DeepSeek",
    "mistralai": "Mistral", "qwen": "Qwen", "moonshotai": "Moonshot",
    "z-ai": "Z.ai", "minimax": "MiniMax", "nvidia": "NVIDIA",
    "tencent": "Tencent", "amazon": "Amazon", "microsoft": "Microsoft",
    "cohere": "Cohere", "perplexity": "Perplexity", "baidu": "Baidu",
    "bytedance": "ByteDance", "xiaomi": "Xiaomi", "ai21": "AI21",
}


def fetch(url, accept):
    request = urllib.request.Request(
        url, headers={"User-Agent": USER_AGENT, "Accept": accept})
    with urllib.request.urlopen(request, timeout=TIMEOUT) as response:
        return response.read().decode("utf-8", "replace")


def fetch_models(url):
    data = json.loads(fetch(url, "application/json"))
    models = data.get("data") if isinstance(data, dict) else data
    return [m for m in models or [] if isinstance(m, dict) and m.get("id")]


def ranked_slugs(html, known):
    """Model ids in the order the rankings page mentions them."""
    # Plain links (href="/deepseek/deepseek-v4") and ids inside the page's
    # embedded data ("permaslug":"deepseek/deepseek-v4-20261001"), possibly
    # with escaped quotes.
    pattern = re.compile(
        r'href="/([a-z0-9][\w.-]*/[\w.:-]+)"'
        r'|(?:slug|permaslug|model)\\?"\s*:\s*\\?"([a-z0-9][\w.-]*/[\w.:-]+)')
    seen, order = set(), []
    for match in pattern.finditer(html):
        slug = known.get(match.group(1) or match.group(2))
        if slug and slug not in seen:
            seen.add(slug)
            order.append((slug, match.end()))
    return order


def usage_near(html, position):
    """The "66.8T tokens"-style figure right after a model link, if any."""
    window = re.sub(r"<[^>]+>", " ", html[position:position + 600])
    match = re.search(r"(\d+(?:\.\d+)?\s?[KMBT])\s*tokens", window)
    return match.group(1).replace(" ", "") if match else ""


def features(model):
    params = set(model.get("supported_parameters") or [])
    inputs = set((model.get("architecture") or {}).get("input_modalities") or [])
    found = []
    if "tools" in params:
        found.append("tools")
    if "image" in inputs:
        found.append("vision")
    if "reasoning" in params or "include_reasoning" in params:
        found.append("reasoning")
    if "structured_outputs" in params or "response_format" in params:
        found.append("JSON")
    if "audio" in inputs:
        found.append("audio")
    return ", ".join(found)


def price(pricing, key):
    try:
        value = float((pricing or {}).get(key))
    except (TypeError, ValueError):
        return -1
    return round(value * 1_000_000, 4) if value >= 0 else -1


def describe(model, usage=""):
    model_id = model["id"]
    author = model_id.split("/", 1)[0]
    name = model.get("name") or model.get("display_name") or model_id
    if ": " in name:  # "OpenAI: GPT-6" -> "GPT-6"
        name = name.split(": ", 1)[1]
    top = model.get("top_provider") or {}
    return {
        "id": model_id,
        "name": name,
        "provider": PROVIDERS.get(author, author.replace("-", " ").title()),
        "context": model.get("context_length") or top.get("context_length")
                   or model.get("max_input_tokens") or 0,
        "max_output": top.get("max_completion_tokens")
                      or model.get("max_tokens") or 0,
        "price_in": price(model.get("pricing"), "prompt"),
        "price_out": price(model.get("pricing"), "completion"),
        "features": features(model),
        "usage": usage,
    }


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    output = os.path.expanduser(sys.argv[1])

    models = fetch_models(MODELS_URL)
    by_id = {m["id"]: m for m in models}
    known = {}
    for m in models:
        known[m["id"]] = m["id"]
        if m.get("canonical_slug"):
            known[m["canonical_slug"]] = m["id"]

    top, source = [], "OpenRouter rankings"
    try:
        html = fetch(RANKINGS_URL, "text/html")
        for slug, position in ranked_slugs(html, known)[:TOP]:
            top.append(describe(by_id[slug], usage_near(html, position)))
    except Exception as error:  # the page is the fragile part
        print(f"rankings page: {error}", file=sys.stderr)

    if len(top) < TOP // 2:
        source = "OpenRouter most popular"
        popular = fetch_models(POPULAR_URL)[:TOP]
        top = [describe(by_id.get(m["id"], m)) for m in popular]

    if not top:
        sys.exit("no models found")

    os.makedirs(os.path.dirname(output) or ".", exist_ok=True)
    fd, temporary = tempfile.mkstemp(dir=os.path.dirname(output) or ".")
    with os.fdopen(fd, "w") as file:
        json.dump({"source": source, "updated": time.time(), "models": top},
                  file, indent=2)
    os.replace(temporary, output)
    print(f"{len(top)} models from {source} -> {output}")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        sys.exit(f"claude_models: {error}")
