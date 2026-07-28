# ColorPicker++

A lightweight Windows color picker written in C++ using the Win32 API.

Press a global hotkey to activate the picker, move the cursor over any pixel, and click to copy its color as a HEX value.

## Features

* Global hotkey: `Ctrl + Shift + C`
* Pixel color sampling from the screen
* Magnified preview around the cursor
* Center marker showing the sampled pixel
* HEX color display
* RGB color display
* Left-click to copy `#RRGGBB` to the clipboard
* Right-click to cancel
* `Esc` to cancel
* No system tray icon
* Exits automatically after picking or canceling
* Multi-monitor support
* Per-monitor DPI awareness

## Usage

1. Launch `ColorPicker++.exe`.
2. Press `Ctrl + Shift + C`.
3. Move the cursor over the color you want to sample.
4. Use the magnified preview to identify the target pixel.
5. Left-click to copy the HEX color to the clipboard.
6. The application exits automatically.

### Cancel

* Right-click to cancel.
* Press `Esc` to cancel.

## Example

The picker displays the sampled color and its values near the cursor:

```text
#3A82F6
RGB(58, 130, 246)
```

The copied clipboard value is:

```text
#3A82F6
```

## Requirements

* Windows 10 or later
* x64 Windows
* Visual Studio 2022 or later
* Desktop C++ development tools

## Build

Open the solution in Visual Studio:

```text
ColorPicker++.sln
```

Select:

```text
Configuration: Release
Platform: x64
```

Then build the solution.

The executable will be generated under:

```text
x64\Release\
```

## Project Structure

```text
ColorPicker++/
├── ColorPicker++.sln
├── ColorPicker++.vcxproj
├── ColorPicker++.vcxproj.filters
└── main.cpp
```

Build output and Visual Studio-generated files are excluded through `.gitignore`.

## Design

ColorPicker++ intentionally avoids a system tray process and external frameworks.

The application uses native Win32 APIs for:

* Global hotkey registration
* Low-level mouse and keyboard hooks
* Screen pixel sampling
* Clipboard access
* Window rendering
* Magnified preview rendering
* Per-monitor DPI awareness

This keeps the application small and lightweight.