# Chip-Boy Contributing Guide

## Radio App

The radio app is defined in `RadioApp.cpp`. It uses a DFPlayer Mini module that reads MP3 files from a microSD card organized into numbered folders.

---

### How the SD Card is Organized

The DFPlayer expects files to be in numbered folders matching the station number, with numbered filenames matching the track number.
SD Card/
├── 01/
│   ├── 001.mp3
│   ├── 002.mp3
│   └── ...
├── 02/
│   ├── 001.mp3
│   └── ...
└── 06/
├── 001.mp3
└── ...
The folder number = station number. The file number = track number. The order of the files in the folder determines which track number they get, so make sure they are named and sorted correctly.

---

### How Stations and Tracks Are Defined in Code

Each station has a track table defined as a `static const TrackInfo` array near the top of `RadioApp.cpp`. For example, station 6 looks like this:

```cpp
static const TrackInfo kTracks06[84] = {
  {"",""},
  {"Helena", "My Chemical Romance"},
  {"What's It Feel Like To Be A Ghost?", "Taking Back Sunday"},
  // ...
};
```

The first entry `{"",""}` is always blank — it's a placeholder because the DFPlayer track numbers start at 1, not 0. Every entry after that is `{"Song Title", "Artist Name"}` and the position in the array is the track number that must match the file on the SD card.

---

### Editing a Song Title or Artist Name

Find the track table for the station you want to edit (e.g. `kTracks06` for station 6) and update the entry directly:

```cpp
// Before
{"Suitcase", "Circa Survive"},

// After
{"Suitcase", "The Matches"},
```

Make sure you do not change the position of the entry in the array, as that would desync the code from the SD card file order.

---

### Adding a Song to an Existing Station

1. Add the MP3 file to the correct folder on the SD card, named with the next available track number (e.g. `084.mp3`).
2. Add a new entry to the end of the track table in `RadioApp.cpp` (before the closing `};`):
```cpp
{"Your Song Title", "Artist Name"},
```
3. Update the array size in the declaration. For example if you now have 84 entries plus the blank placeholder:
```cpp
// Change this:
static const TrackInfo kTracks06[84] = {

// To this:
static const TrackInfo kTracks06[85] = {
```
4. Update `radioTrackMaxForStation()` to return the new track count for that station:
```cpp
if (station == 6) return 84;
```

---

### Adding a New Station

1. Create a new numbered folder on the SD card (e.g. `07/`) and add your MP3 files inside it numbered from `001.mp3`.
2. Add a new track table in `RadioApp.cpp` following the same pattern as the others:
```cpp
static const TrackInfo kTracks07[N] = {
  {"",""},
  {"First Song", "Artist"},
  {"Second Song", "Artist"},
  // ...
};
```
Where `N` is the number of tracks plus 1 for the blank placeholder.
3. Add a return value for the new station in `radioTrackMaxForStation()`:
```cpp
if (station == 7) return N - 1;
```
4. Add a lookup for the new station in `radioGetTrackInfo()`:
```cpp
if (station == 7) return kTracks07[track];
```
5. Update `RADIO_STATION_MAX`:
```cpp
const uint8_t RADIO_STATION_MAX = 7;
```
6. Update `RADIO_SHUFFLE_MAX` to be at least as large as your biggest station's track count:
```cpp
static const uint8_t RADIO_SHUFFLE_MAX = 84;
```

---

### Things to Keep in Mind

- The blank `{"",""}` placeholder at index 0 of every track table is required. Do not remove it.
- The array size in the declaration must always equal the number of tracks plus 1.
- The track count returned by `radioTrackMaxForStation()` must always equal the number of actual tracks, not counting the blank placeholder.
- `RADIO_SHUFFLE_MAX` must always be greater than or equal to the track count of your largest station or the device will crash when shuffle is enabled on that station.
- SD card file names must be zero-padded numbers (e.g. `001.mp3`, `042.mp3`) and sorted correctly inside their folder. The DFPlayer reads them in filesystem order.