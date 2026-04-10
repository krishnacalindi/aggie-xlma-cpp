# Contributing to Aggie XLMA

Thanks for your interest in contributing, please reach out to [Dr.Timothy Logan](https://artsci.tamu.edu/atmos-science/contact/profiles/timothy-logan.html) for additional information. This document includes a brief outline of the architecture to help you get started. 

## Getting Started

Check out `todo.md` for a full list of planned features and improvements. Items marked as minor are great starting points for new contributors.

## Architecture Overview

The app is built on four main systems that work together:

**DuckDB** handles all data storage and filtering. LMA data is loaded into an in-memory `lma` table, ENTLN lightning data into an `entln` table. All filtering (attribute, spatial, time) is done via SQL UPDATE statements on a `plot` boolean column. Only rows where `plot = true` are ever rendered.

**Graphics** (`graphics.h / graphics.cpp`) owns all OpenGL state — VBOs, VAOs, FBOs, textures, and shaders. Data flows from DuckDB query results into VBOs via `ProcessResult`, `ProcessEntlnResult`, and `ProcessColor`. Each of the five plots (`time_alt`, `lon_alt`, `alt_hist`, `lon_lat`, `alt_lat`) renders into its own FBO texture which ImGui then displays as an image.

**State** (`state.h / state.cpp`) is the central coordinator. It owns the Graphics instance, filter parameters, animation state, and style. All filtering functions live here — they run the SQL, then call into Graphics to update the GPU data and re-render.

**ImGui** (`main.cpp`) handles all UI. The `RenderUI()` function is called every frame and builds the menu bar, tools panel, and plot panels. Plot interactivity (zoom, pan, polygon selection) is handled inline in the plot lambda.

## Key Files

- `main.cpp` — UI, input handling, main loop
- `state.h / state.cpp` — filtering logic, animation
- `graphics.h / graphics.cpp` — OpenGL rendering pipeline
- `shader.h / shader.cpp` — GLSL shaders for VHF, ENTLN, map, stations
- `select.h / select.cpp` — polygon selection logic
- `style.h / style.cpp` — ImGui themes and display settings

## Notes

- The main display image is from the July 12, 2022 TRACER case. It was the very first case I worked on with Dr. Logan. If you can, please continue using it :).