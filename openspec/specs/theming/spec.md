# Theming

Two themes — white (`#ffffff`) and black (`#000000`) — defined in `src/Theme.cpp`.
Contrast is treated as a hard requirement rather than an aesthetic preference: every colour pair
is expressed as a WCAG 2.1 contrast ratio and pinned by `tests/test_theme.cpp`. Changing a colour
that breaks a threshold fails the build.

Thresholds: body text ≥ 7:1 (AAA), secondary text and links ≥ 4.5:1 (AA), non-text elements such
as borders ≥ 3:1.

## Palette

### Requirement: Themes are pure white and pure black
The white theme's background SHALL be `#ffffff` and the black theme's `#000000`. The black theme
SHALL be the default.

#### Scenario: Defaults
- GIVEN a first run with no saved settings
- WHEN the window opens
- THEN the black theme is active

### Requirement: The theme is applied unconditionally at startup
The application palette SHALL be applied once at startup even when the resolved mode equals the
initial value. Skipping it leaves the app on the system (GTK) palette, which does not match the
document colours — observed as a slate-blue window that was neither theme.

#### Scenario: Startup with the default mode
- GIVEN saved settings that select the same mode the application starts in
- WHEN the window is constructed
- THEN `qApp` still receives the theme palette

### Requirement: Every palette role is set
`Theme::palette()` SHALL set the full set of colour roles, not only `Window`, `Base` and `Text`.
`QTabBar` and `QMenuBar` paint with `Button`/`ButtonText`; leaving those at their defaults
produces invisible tab labels and an invisible menu bar on the black theme.

#### Scenario: Role pairs are readable
- GIVEN either theme's palette
- WHEN each pair is measured
- THEN `WindowText`/`Window` ≥ 4.5:1, `Text`/`Base` ≥ 7:1, `Text`/`AlternateBase` ≥ 7:1,
  `ButtonText`/`Button` ≥ 4.5:1, `ToolTipText`/`ToolTipBase` ≥ 4.5:1,
  `HighlightedText`/`Highlight` ≥ 4.5:1, `Link`/`Base` ≥ 4.5:1,
  `PlaceholderText`/`Base` ≥ 3:1, `Mid`/`Window` ≥ 3:1, and disabled `WindowText` ≥ 3:1

#### Scenario: No default light role leaks into the black theme
- GIVEN the black theme palette
- WHEN background roles (`Window`, `Base`, `AlternateBase`, `Button`, `ToolTipBase`) are measured
- THEN each has a relative luminance below 0.2, and each text role above 0.3

## Document colours

### Requirement: Inline code is visually distinct from links
Inline `code` SHALL have its own foreground and a tinted background chip, and its hue SHALL
differ from the link colour by at least 60°.

Distinguishability SHALL NOT be judged by contrast ratio: that measures luminance only, so purple
`#6f42c1` and blue `#0b57d0` score 1.13:1 despite being obviously different colours. Using
contrast as the test would push the fix toward changing lightness instead of hue.

Colour is also not the only cue — links are underlined and inline code is monospaced with a
background chip — so the distinction survives for readers with colour vision deficiencies.

#### Scenario: Inline code colours
- GIVEN either theme
- WHEN inline code colours are measured
- THEN foreground against its chip is ≥ 4.5:1, the chip against the page is between 1.05:1 and
  2:1, the hue differs from the link colour by ≥ 60°, and the foreground differs from body text

### Requirement: Syntax highlighting stays readable
Every colour emitted by the highlighter SHALL have at least 4.5:1 contrast against the code block
background, in both themes and for every supported language.

#### Scenario: All languages, both themes
- GIVEN highlighted output for every supported language in both themes
- WHEN every `color:` in the output is measured against the `<pre>` background declared in the
  same output
- THEN each pair is at least 4.5:1

### Requirement: Stylesheets use named tokens
Generated stylesheets SHALL be built by replacing named tokens (`@TEXT@`, `@CODE_FG@`, …) and
SHALL NOT use positional `%1` arguments.

`QString::arg`'s multi-argument overload substitutes the *lowest-numbered markers present* in
order rather than mapping `%N` to the Nth argument, so dropping one number shifts everything
after it. That happened: inline code's `background-color` received the foreground colour, and the
test still passed because the contrast fixup then replaced the foreground with a readable value.

#### Scenario: No leftovers
- GIVEN either theme's document stylesheet
- WHEN it is generated
- THEN it contains no `@` and no `%<digit>`, and every theme colour appears in it

## Tab states

### Requirement: The selected tab is unmistakable
The selected tab SHALL use the page background, the body text colour, bold weight and a top
accent line; unselected tabs SHALL use a background one step away from the page and the secondary
text colour. `QTabBar`'s default selected state differs by only a slight background shade, which
users reported as not being able to tell where focus was.

#### Scenario: Tab contrast
- GIVEN either theme
- WHEN tab colours are measured
- THEN selected text against its background is ≥ 7:1, unselected text against its background is
  ≥ 4.5:1, the two backgrounds differ by ≥ 1.2:1, selected text is more prominent than
  unselected, and the accent line is ≥ 3:1 against the page

## Fallback

### Requirement: A readable foreground exists for any background
`Theme::readableOn(bg, mode)` SHALL return the theme's text colour when it reaches 4.5:1 against
`bg`, and otherwise whichever of black or white contrasts more.

#### Scenario: Theme text would be invisible
- GIVEN the black theme and a white background
- WHEN `readableOn` is called
- THEN it returns a dark colour reaching at least 4.5:1

#### Scenario: Arbitrary backgrounds
- GIVEN every grey from 0 to 255 in steps of 5, plus several saturated colours
- WHEN `readableOn` is called for each in both themes
- THEN the returned colour always reaches at least 4.5:1
