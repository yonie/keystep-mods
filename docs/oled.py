#!/usr/bin/env python
"""Draws the README's pictures as SVG: the OLED popups from their text (a title bar over a big value, the way the KeyStep
Pro shows its encoder popups) and an abstract row of the 16 step LEDs. Stylised drawings only: never the device's
shape, never a capture.

    python docs/oled.py          (writes docs/img/oled_*.svg and docs/img/steps_flux.svg)
"""
import os

SCREENS = {
    'flux_range': ('Range', '12 semi'), 'flux_loop_length': ('Loop Length', '8 steps'),
    'flux_density': ('Density', '75 %'), 'flux_change': ('Change', '10 %'), 'flux_riff': ('Riff', 'Turn for new!'),
    'flux_last_step': ('Loop Length', '6 steps'),
    'pulse_dynamics': ('Dynamics', '30 %'), 'pulse_steps': ('Steps', '16 steps'), 'pulse_pulses': ('Pulses', '4 pulses'),
    'pulse_rotate': ('Rotate', '0 steps'), 'pulse_chance': ('Chance', '100 %'),
}

SVG = '''<svg xmlns="http://www.w3.org/2000/svg" width="256" height="128" viewBox="0 0 256 128">
  <rect x="0.5" y="0.5" width="255" height="127" rx="10" fill="#15181d" stroke="#2b3037"/>
  <rect x="10" y="10" width="236" height="26" rx="3" fill="#d6dde4"/>
  <text x="128" y="29" text-anchor="middle" font-family="ui-monospace, Consolas, Menlo, monospace" font-size="19"
        font-weight="700" fill="#15181d">{title}</text>
  <text x="128" y="88" text-anchor="middle" font-family="ui-monospace, Consolas, Menlo, monospace" font-size="{size}"
        font-weight="700" fill="#e8eef4" textLength="{fit}" lengthAdjust="spacingAndGlyphs">{value}</text>
</svg>
'''


# a Flux loop of 9 steps: n = note, r = rest, p = playhead, c = just changed; the rest of the 16 are unlit
STEPS = 'nrnpnncrn' + '-' * 7
COLOUR = {'n': '#1fb39b', 'r': '#2b3a3f', 'p': '#f2f5f7', 'c': '#d43aa5', '-': '#1a1d22'}


def steps_svg():
    cells = ''.join('<rect x="%d" y="8" width="40" height="40" rx="7" fill="%s"%s/>' % (
        8 + i * 48, COLOUR[c], ' stroke="#2b3037"' if c == '-' else '') for i, c in enumerate(STEPS))
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="776" height="56" viewBox="0 0 776 56">\n'
            '  <rect x="0.5" y="0.5" width="775" height="55" rx="10" fill="#101215"/>\n  %s\n</svg>\n' % cells)


def main():
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'img')
    os.makedirs(out, exist_ok=True)
    for name, (title, value) in SCREENS.items():
        size = 34 if len(value) <= 9 else 26
        fit = min(228, int(len(value) * size * 0.6))
        with open(os.path.join(out, 'oled_%s.svg' % name), 'w', newline='\n') as f:
            f.write(SVG.format(title=title, value=value.replace('&', '&amp;'), size=size, fit=fit))
    with open(os.path.join(out, 'steps_flux.svg'), 'w', newline='\n') as f:
        f.write(steps_svg())
    print('wrote %d screens and the step row to %s' % (len(SCREENS), out))


if __name__ == '__main__':
    main()
