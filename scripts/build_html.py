import os
import re
import gzip
import hashlib
import base64

SRC_DIR = "src"
# Shared web locale helper. Each page carries one TENOR_WEB_I18N marker inside
# its script block; the helper and its vi/en-AU/zh-Hans catalogues live in a
# single include so the firmware ships exactly one implementation.
WEB_I18N_TOKEN = "/* TENOR_WEB_I18N */"
WEB_I18N_INCLUDE = "src/network/html/WebI18n.inc"

def minify_html(html: str) -> str:
    # Tags where whitespace should be preserved
    preserve_tags = ['pre', 'code', 'textarea', 'script', 'style']
    preserve_regex = '|'.join(preserve_tags)

    # Protect preserve blocks with placeholders
    preserve_blocks = []
    def preserve(match):
        preserve_blocks.append(match.group(0))
        return f"__PRESERVE_BLOCK_{len(preserve_blocks)-1}__"

    html = re.sub(rf'<({preserve_regex})[\s\S]*?</\1>', preserve, html, flags=re.IGNORECASE)

    # Remove HTML comments
    html = re.sub(r'<!--.*?-->', '', html, flags=re.DOTALL)

    # Collapse all whitespace between tags
    html = re.sub(r'>\s+<', '><', html)

    # Collapse multiple spaces inside tags
    html = re.sub(r'\s+', ' ', html)

    # Restore preserved blocks
    for i, block in enumerate(preserve_blocks):
        html = html.replace(f"__PRESERVE_BLOCK_{i}__", block)

    return html.strip()

def sanitize_identifier(name: str) -> str:
    """Sanitize a filename to create a valid C identifier.

    C identifiers must:
    - Start with a letter or underscore
    - Contain only letters, digits, and underscores
    """
    # Replace non-alphanumeric characters (including hyphens) with underscores
    sanitized = re.sub(r'[^a-zA-Z0-9_]', '_', name)
    # Prefix with underscore if starts with a digit
    if sanitized and sanitized[0].isdigit():
        sanitized = f"_{sanitized}"
    return sanitized

def fail(message):
    """Abort the build with an actionable message.

    A page that references the shared locale include must never be emitted with
    its marker left in place: the browser would receive broken JavaScript and
    the failure would only appear on the device.
    """
    raise SystemExit(f"build_html.py: {message}")


def read_web_i18n():
    if not os.path.isfile(WEB_I18N_INCLUDE):
        fail(
            f"{WEB_I18N_INCLUDE} is missing but a page references the shared locale "
            f"include ({WEB_I18N_TOKEN}); restore that file before building."
        )
    with open(WEB_I18N_INCLUDE, encoding="utf-8") as include_file:
        include = include_file.read()
    if not include.strip():
        fail(f"{WEB_I18N_INCLUDE} is empty; the page would receive broken locale JavaScript.")
    if WEB_I18N_TOKEN in include:
        fail(f"{WEB_I18N_INCLUDE} contains {WEB_I18N_TOKEN}; the include cannot reference itself.")
    if "</script" in include.lower():
        fail(
            f"{WEB_I18N_INCLUDE} contains a </script> tag; it is injected inside a script "
            "block and would break out of it."
        )
    return include


def inject_web_i18n(content, file_path):
    """Substitute the shared locale include at the page's TENOR_WEB_I18N marker."""
    if WEB_I18N_TOKEN not in content:
        return content
    marker_index = content.index(WEB_I18N_TOKEN)
    open_tag = content.rfind("<script", 0, marker_index)
    close_tag = content.rfind("</script", 0, marker_index)
    if open_tag == -1 or close_tag > open_tag:
        fail(
            f"{file_path} places {WEB_I18N_TOKEN} outside a <script> block; "
            "the include is JavaScript."
        )
    content = content.replace(WEB_I18N_TOKEN, read_web_i18n())
    if WEB_I18N_TOKEN in content:
        fail(f"{file_path} still contains {WEB_I18N_TOKEN} after substitution.")
    return content


for root, _, files in os.walk(SRC_DIR):
    for file in files:
        if file.endswith((".html", ".js", ".css")):
            file_path = os.path.join(root, file)
            with open(file_path, "r", encoding="utf-8") as f:
                content = f.read()

            if "<!--TENOR_BRAND-->" in content:
                with open("src/network/html/BrandHeader.inc", encoding="utf-8") as brand:
                    content = content.replace("<!--TENOR_BRAND-->", brand.read())
            if "/* TENOR_WEB_TOKENS */" in content:
                with open("src/network/html/assets/BrandTokens.css.inc", encoding="utf-8") as tokens:
                    content = content.replace("/* TENOR_WEB_TOKENS */", tokens.read())
            if "/* TENOR_WEB_FONT */" in content:
                with open("src/network/html/assets/Geist.woff2", "rb") as font:
                    data = base64.b64encode(font.read()).decode("ascii")
                content = content.replace("/* TENOR_WEB_FONT */", "@font-face{font-family:Geist;src:url(data:font/woff2;base64," + data + ") format('woff2');font-weight:100 900;font-display:swap}")
            content = inject_web_i18n(content, file_path)
            # Only minify HTML files; JS files are typically pre-minified (e.g., jszip.min.js)
            if file.endswith(".html"):
                processed = minify_html(content)
            else:
                processed = content

            # Compress with gzip (compresslevel 9 is maximum compression)
            # mtime=0 keeps the output reproducible across builds
            # IMPORTANT: we don't use brotli because Firefox doesn't support brotli with insecured context (only supported on HTTPS)
            compressed = gzip.compress(processed.encode('utf-8'), compresslevel=9, mtime=0)

            # Create valid C identifier from filename
            # Use appropriate suffix based on file type
            suffix = "Html" if file.endswith(".html") else "Css" if file.endswith(".css") else "Js"
            base_name = sanitize_identifier(f"{os.path.splitext(file)[0]}{suffix}")
            header_path = os.path.join(root, f"{base_name}.generated.h")

            with open(header_path, "w", encoding="utf-8") as h:
                h.write(f"// THIS FILE IS AUTOGENERATED, DO NOT EDIT MANUALLY\n\n")
                h.write(f"#pragma once\n")
                h.write(f"#include <cstddef>\n\n")

                # Write the compressed data as a byte array
                h.write(f"constexpr char {base_name}[] PROGMEM = {{\n")

                # Write bytes in rows of 16
                for i in range(0, len(compressed), 16):
                    chunk = compressed[i:i+16]
                    hex_values = ', '.join(f'0x{b:02x}' for b in chunk)
                    h.write(f"  {hex_values},\n")

                h.write(f"}};\n\n")
                h.write(f"constexpr size_t {base_name}CompressedSize = {len(compressed)};\n")
                h.write(f"constexpr size_t {base_name}OriginalSize = {len(processed)};\n")

                # ETag derived from the compressed payload. The content is
                # immutable at runtime (baked into flash at build time), so a
                # strong ETag keyed on the bytes is safe and stable. Browsers
                # echo it back as If-None-Match, enabling 304 responses.
                etag = hashlib.sha256(compressed).hexdigest()[:16]
                h.write(f'constexpr const char* {base_name}ETag = "\\"{etag}\\"";\n')

            print(f"Generated: {header_path}")
            print(f"  Original: {len(content)} bytes")
            print(f"  Minified: {len(processed)} bytes ({100*len(processed)/len(content):.1f}%)")
            print(f"  Compressed: {len(compressed)} bytes ({100*len(compressed)/len(content):.1f}%)")
