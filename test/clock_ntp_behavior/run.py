#!/usr/bin/env python3
"""Exercise complete production clock and the current HTTP request flow."""
import argparse
import hashlib
import json
import pathlib
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', required=True, type=pathlib.Path)
    parser.add_argument('--cxx', default='c++')
    args = parser.parse_args()
    here = pathlib.Path(__file__).resolve().parent
    repo = here.parents[1]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    # This harness stubs hardware/TLS boundaries and compiles the complete
    # current function, including the epoch gate, CA setup, and body delivery.
    http = (repo / 'src/network/HttpDownloader.cpp').read_text()
    start = http.index('struct Sink {')
    end = http.index('\n#endif', http.index('HttpDownloader::DownloadError runGetWolf'))
    flow = http[start:end].replace('#if defined(FREEINK_NET_WOLFSSL)\n', '')
    (output / 'production-http-flow.inc').write_text(flow)
    clock = repo / 'lib/hal/HalClock.cpp'
    (output / 'source-hashes.json').write_text(json.dumps({
        'HalClock.cpp': hashlib.sha256(clock.read_bytes()).hexdigest(),
        'HttpDownloader.cpp': hashlib.sha256(http.encode()).hexdigest(),
        'http-flow': hashlib.sha256(flow.encode()).hexdigest(),
    }, indent=2) + '\n')
    binary = output / 'clock-ntp-regression'
    command = [args.cxx, '-std=c++17', '-Wall', '-Wextra', '-Werror',
               '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
               '-I' + str(here / 'stubs'), '-I' + str(repo / 'lib/hal'),
               '-I' + str(repo / 'freeink-sdk/libs/network/SecureNet/include'),
               '-I' + str(output), str(clock), str(here / 'ClockNtpBehavior.cpp'),
               '-o', str(binary)]
    subprocess.run(command, check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    (output / 'result.log').write_text(result.stdout + result.stderr)
    print(result.stdout + result.stderr, end='')
    return result.returncode


if __name__ == '__main__':
    raise SystemExit(main())
