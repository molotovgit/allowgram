import argparse
import hashlib
from pathlib import Path
import re


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--patch', action='store_true')
    args = parser.parse_args()
    telegram = Path(__file__).resolve().parents[2]
    source = telegram / 'lib_ui/emoji.txt'
    target = telegram / 'SourceFiles/main/allowlist_emoji_ranges.inc'
    sequences = re.findall(r'"([^"]+)"', source.read_text(encoding='utf-8'))
    excluded = set(map(ord, '0123456789#*')) | {0x200D, 0xFE0E, 0xFE0F}
    scalars = sorted({ord(char) for sequence in sequences for char in sequence
                      if ord(char) not in excluded and not 0xE0020 <= ord(char) <= 0xE007F})
    ranges = []
    for scalar in scalars:
        if ranges and ranges[-1][1] + 1 == scalar:
            ranges[-1][1] = scalar
        else:
            ranges.append([scalar, scalar])
    expected = ''.join(f'\t{{ 0x{start:X}, 0x{end:X} }},\n' for start, end in ranges)
    if args.patch:
        print('*** Begin Patch\n*** Add File: Telegram/SourceFiles/main/allowlist_emoji_ranges.inc')
        print(''.join('+' + line + '\n' for line in expected.splitlines()), end='')
        print('*** End Patch')
    else:
        assert target.read_text(encoding='utf-8') == expected, 'Emoji table differs from Telegram data'
        assert len(sequences) >= 4800 and 0x20E3 in scalars
        print(f'{len(sequences)} sequences; {len(scalars)} scalars; {len(ranges)} exact ranges')
        print('Telegram emoji data SHA256: ' + hashlib.sha256(source.read_bytes()).hexdigest())


if __name__ == '__main__':
    main()
