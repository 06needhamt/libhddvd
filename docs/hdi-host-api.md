# HDi host API used by disc scripts

HDi scripts are ECMAScript 3 source (`docs/formats/hdi-script.md`). Running
them needs an ECMAScript engine plus the host objects the player provides. The
full host API is defined in the DVD Forum's Advanced Content specification,
which is not public. This note lists the part that real scripts actually use.

## How it was measured

`tools/inventory_hdi_scripts.py` reads each disc's extracted `.js` and `.xmu`
members, skips protected scripts, and reports:

- **host globals:** names a script reads that no script declares or assigns
  and that are not ECMAScript built-ins, each with its member chains (for
  example `Player.playlist.titles[].jump()`);
- **events:** every type passed to `addEventListener`, with its origin:
  `script` (created with `createEvent`/`initEvent` or passed to a script
  helper), `markup` (declared by an `<event name=...>` in markup timing), or
  `other` (raised by nothing scanned: the player, or code that was not
  scanned);
- **methods:** methods called on other objects that no script defines. This is
  a heuristic for host methods reached through variables, such as elements,
  event objects and HTTP clients.

The report holds identifiers and counts only, never source text. To repeat it,
extract each disc's archives with `hddvd-acaextract` to a directory outside
the repository and pass one directory per disc:

```sh
python3 tools/inventory_hdi_scripts.py /scratch/disc-a /scratch/disc-b ...
```

## Results for the five ripped discs

126 readable scripts (4.3 MB of UTF-16 source) from Fast & Furious and both
300 discs. The 5 scripts on Sabra and South Park are protected and were
skipped. The tool found 20 globals, 356 distinct member chains, 165 listened
event types and 22 heuristic methods.

### Global objects

| Global | Uses | Members used |
|---|---:|---|
| `document` | 2,278 | `getElementById()`, `createEvent()`, `body`, `getProperties()`, `setXPathVariable()`, and elements by id as properties (`document.okBtn`, about 100 ids) |
| `Player` | 817 | see below |
| `application` | 281 | `addEventListener()`, `createTimer()`, `advancedApplications[]` (`id`, `activate()`), `thisAdvancedApplication` (`zOrder`, `inactivate()`), `attributes.id`, `moveToTop()`, `moveToBottom()` |
| `FileIO` | 104 | `openTextFile()`, `getFiles()`, `getDirectoryInfo()`, `createDirectory()`, `copy()`, `move()`, `remove()`, and status constants (`SUCCEEDED`, `FAILED`, `FILE_NOT_FOUND`, ...) |
| `XMLParser` | 88 | `parse()`, `parseString()`, `write()`, `status`, and constants (`OK`, `READY`, `UTF8`, `UTF16`, `PARSING_ERR`, ...) |
| `Network` | 56 | `createHTTPClient()`, `HTTP_GET`, `HTTP_POST` |
| `Diagnostics` | 21 | `trace()`, `listeners`, `TRACE_LEVEL_INFO` |
| `PersistentStorageManager` | 15 | `getPersistentStorageDevices()`, `contentId` |
| `PersistentStorageDevice` | 2 | `SUCCEEDED` |
| `Node` | 2 | `ELEMENT_NODE` |

Global functions and constants: `addEventListener()` (241 uses),
`createTimer()` (115), `dispatchEvent()` (73), `setMarkupLoadedHandler()` (32),
`TIMER_APPLICATION` (28), `removeEventListener()` (19) and `createEvent()` (10).

`elapsedTimes`, `lastMovie` and `runTests` are also reported, but they are
script variables that the shipped files never define. They appear in debug or
leftover code and are not host objects.

### `Player`

| Area | Members used |
|---|---|
| Playlist | `playlist.titles[]` and `playlist.titles.<id>` (`jump()`, `chapters[].jump()`, `attributes.titleDuration`, `audioTracks`, `subtitleTracks`); `playlist.currentTitle` (`attributes.id`, `elapsedTime`, `chapters`, `jump()`); `currentChapter.number`; `play()`, `pause()`, `load()`; `playState` with `PLAYSTATE_PLAY` and `PLAYSTATE_PAUSE` |
| Tracks | `track.selectAudioTrackNumber()`, `selectAudioLanguage()`, `selectSubtitleTrackNumber()`, `selectSubtitleLanguage()`, `currentAudioTrackNumber`, `currentSubtitleTrackNumber`, `selectedAudioTrackNumber`, `selectedAudioLanguageCode`, `selectedSubtitleLanguageCode` |
| Video and audio | `video.main`, `video.sub`, `createVideoScale()`, `audio.main`, `audio.sub`, `audio.effect`, `subtitle.visible` |
| Settings | `generalParameters.getValue()`, `generalParameters.setValue()`, `menuLanguage`, `capabilities.audio`, `capabilities.network`, `standardContentPlayer.getSPRM()` |
| Constants | `FINISHED`, `FILE_NOT_FOUND`, `WRONG_FORMAT` |

### Elements, state and style

Elements come from `document.getElementById()` or `document.<id>`. Scripts use
`state.focused`, `state.value`, `state.getProperty()`, `state.setProperty()`
and `state.unsetProperty()`; `style.getProperty()`, `style.setProperty()`,
`style.unsetProperty()` and `style.animateProperty()`; `parentNode`; and these
style properties directly: `x`, `y`, `width`, `height`, `display`,
`visibility`, `opacity`, `color`, `backgroundColor`, `backgroundImage`,
`backgroundFrame`, `backgroundPositionHorizontal` and `navUp`/`navDown`/
`navLeft`/`navRight`. These are the same properties `hddvd_xmu_styles` models,
so a runtime can write through to the computed styles and rebuild the scene.

Heuristic methods (called on variables, defined by no script): DOM methods
(`getAttribute()` 434 uses, `setAttribute()`, `getAttributeNS()`,
`removeAttribute()`, `getElementsByTagName()`, `appendChild()`,
`removeChild()`, `createElement()`, `createTextNode()`, `hasChildNodes()`,
`getNamedItem()`, `evaluateXPath()`), state and style methods
(`animateProperty()`, `getProperty()`, `unsetProperty()`, `changeLayout()`),
events (`initEvent()`), HTTP (`send()`, `sendString()`, `setHeader()`,
`getResponseString()`) and audio (`setMixing()`).

### Events

Of the 165 listened event types, 104 come from markup timing and 35 from
scripts. Of the 26 others, 12 look like player events: they share one
snake_case naming style, and the first six of them appear on all three discs:

`controller_key_down`, `controller_key_up`, `scheduled_event`, `play_state`,
`audio_track`, `subtitle_track`, `title_begin`, `title_end`, `chapter`,
`clip_begin`, `clip_end`, `menubar_command_request`.

The remaining 14 are Fast & Furious names (`resetMenus`, `switchEnhanced`,
`focusCorrectSub`, ...) that nothing scanned raises. They are probably raised
by markup or scripts in archives that were not ripped, or by nothing at all.

## What the runtime provides

`hddvd_runtime` (P4-04) provides `document`, elements with `state` and
`style`, `application` listeners and dispatch, events, `createTimer()` and
`animateProperty()`, plus the global functions of the same names and
`setMarkupLoadedHandler()`. `include/hddvd.h` documents each member.
`src/hdi_prelude.js` implements them over natives in `src/runtime_dom.c`.
`Player`, `FileIO`, `XMLParser`, `Network`, `PersistentStorageManager`,
`Diagnostics` and the application lifecycle members are P4-05. Markup timing
(cues, `<event>` elements, markup animations and `setXPathVariable()`) is
not modeled.

### Model choices to confirm against a player

- **Key defaults:** a compass key moves the focus along the focused element's
  nav property, and Enter sets `state.actioned` while held, unless a listener
  calls `preventDefault()`.
- **Timers:** frames in `createTimer("HH:MM:SS:FF", ...)` count at 60000/1001
  per second, as in playlists. Values up to frame 30 appear on disc, so the
  rate is above 30. The clock argument (`1`, `2` or `TIMER_APPLICATION`) is
  ignored, and the value of `TIMER_APPLICATION` is a placeholder.
- **Animations:** `animateProperty()` spaces its keyframes evenly over the
  duration. Lengths and numbers interpolate linearly; other values switch at
  each keyframe.
- **State properties:**
  - `state.unsetProperty()` keeps the value. Scripts call it right after
    setting a value, which suggests it hands the property back to markup
    timing.
  - `enabled` defaults to `"true"`.
- **Initial style values:** `"auto"` for display, `"visible"` for
  visibility, `"1"` for opacity, and `""` otherwise.

### Found by running disc applications

`hddvd-menurun` ran all 52 applications in the five discs' archives, with a
throwaway local stand-in for `Player` (kept outside the repository). Every
readable script evaluates, except 16 that need `PersistentStorageManager`,
`XMLParser` or a `Player` member at load time. The runs showed these
behaviors, which the runtime now follows:

- Some scripts dispatch `document.createEvent("type")` directly, without
  `initEvent()`, so `createEvent()` gives the event its argument as type.
- Key listeners read `evt.target.core.id`, so a key event's target is the
  focused element, and elements have a `core` attribute group.
- Listeners for markup events read `evt.target.attributes.getNamedItem("id").value`.
- Scripts set `style.x = ""`, which removes the script's value, as in CSS.

These remain open:

- A key listener on one disc (a Toshiba player workaround, per its comment)
  tests `evt.target.application`, which must be truthy when no element has
  focus. What that target is remains unconfirmed.
- The rest of the failures are P4-05 members:
  - `application.thisAdvancedApplication`, `advancedApplications` and
    `moveToTop()`;
  - `Player` playlist and audio members.
- `document.setXPathVariable()` belongs to markup timing, which is not
  modeled.

## What this means for a runtime

- **Menus need** `document`, elements and their state and style, `Player`
  playlist, title, chapter and track control, `application` events and timers,
  and the global event functions. All three readable discs use these.
- **Only the 300 discs' online features need** `FileIO`, `XMLParser`,
  `Network` and `PersistentStorageManager`. These can start as stubs that
  report failure, which the libhddvd sandbox rules (no file or network access by
  default) require anyway.
- Calls the runtime does not implement must raise a visible error, never
  silently succeed (P4-05).
