# Reference interaction traces

P4-06 and P5-04 need to know what a conforming HD DVD player does with a menu.
That cannot be worked out from the disc or produced by libhddvd itself. It has
to be recorded from a player. This note says what to record, in which format,
and how to replay the recording against libhddvd.

The replay harness exists and is tested on a hand-written trace of the
fixture application. No trace from a player exists yet.

## The trace file

A trace is one JSON file that follows `tests/reference_trace.schema.json`. It
describes one application (one menu) as a set of states and the transitions
between them.

**A state** is what the player shows while it waits for input:

| Member | Meaning |
|---|---|
| `id` | A name you choose, unique in the trace. Two states may look the same and still differ (a menu with a timer running, for example). |
| `focused_element` | The markup `id` of the focused element, or `null`. |
| `visible_elements` | The markup `id`s of every element shown, in any order. |
| `screenshot_sha256` | SHA-256 of a screenshot of the state. Record it now; the replay does not compare it yet. |
| `title`, `position_90khz` | Optional: the title playing and its time, when the state depends on them. |

**A transition** is one event and what it leads to:

| Member | Meaning |
|---|---|
| `from`, `to` | State ids. The `open` transition's `from` is not a state; use any text, such as `"closed"`. |
| `event` | `open`, `close`, `up`, `down`, `left`, `right`, `activate`, `timer` or `title_end`. |
| `wait_ms` | For `timer` only: how long after the previous event the change happened. 1000 when left out. |
| `host_commands` | The playback commands the event caused, in order. Each is the text `hddvd-menurun` prints after `command:`, for example `jump title=Feature time=00:00:01:00`. An empty list when the event caused none. |

A trace has exactly one `open` transition. Record every transition you can
reach, including keys that change nothing (the destination is then the source
state): a key that libhddvd acts on but the player ignores is a difference
too.

The markup ids come from the application's markup, which `hddvd-menuinfo`
lists. The trace holds ids, hashes and command names only: no image, script
or markup content from the disc.

## Recording

- **Player:** a standalone HD DVD player or a software player of the format's
  time, with a disc you own. Put the player's name and version in `player`.
- **Capture:** photograph or capture the screen in each state, and note the
  focused and visible elements from it. Hash the capture for
  `screenshot_sha256`; keep the captures outside the repository.
- **Commands:** note what playback does after each event (a title starts,
  playback pauses, a track changes) and write it in `hddvd-menurun`'s wording.
  Run `hddvd-menurun` on the same application to see that wording.
- **Timers:** when a state changes without input, record a `timer` transition
  with the delay in `wait_ms`.

Keep the trace outside the source tree, like the reference archive.

## Replaying

```sh
python3 tests/replay_reference_trace.py build/hddvd-menurun \
    --trace menu.trace.json --archive MENU.ACA --manifest MENU.XMF \
    [--playlist VPLST000.XPL [--title ID]] [--report report.json]
```

For every transition the script starts the application afresh, follows the
shortest recorded path to the transition's source state, performs the event,
and compares libhddvd's focus, visible elements and commands with the
recorded destination. It prints one line per transition and exits 0 only when
all of them match.

- **Visible elements** are the elements with an id that are visible in the
  scene built from the runtime's styles, as are all their ancestors
  (`hddvd-menurun --state`).
- **Not replayed:** `close` has no `hddvd-menurun` step, and a state no
  recorded path reaches cannot be tested. Both count as failures, so a
  passing run covers the whole trace.
- **Screenshots** are not compared: libhddvd draws nothing itself. Comparing
  display lists with screenshots is P5-04's remaining work.

With `-DHDDVD_ENABLE_REFERENCE_TESTS=ON`, the `reference-trace` CTest runs
the same replay from the environment and is skipped when it names no trace:

| Variable | Value |
|---|---|
| `HDDVD_REFERENCE_TRACE` | the trace file |
| `HDDVD_REFERENCE_ACA` | the application's archive |
| `HDDVD_REFERENCE_MANIFEST` | the manifest's member name |
| `HDDVD_REFERENCE_PLAYLIST`, `HDDVD_REFERENCE_TITLE` | optional playlist file and title id |
| `HDDVD_REFERENCE_TRACE_REPORT` | optional path for the JSON report |

`tests/fixtures/app/app.trace.json` is a trace of the fixture application
written by hand from its script, with placeholder hashes. It tests the
harness only (the `replay-app.trace` CTest; `replay-app.trace-wrong` checks
that differences are reported).
