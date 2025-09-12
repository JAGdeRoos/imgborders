As seen in 𝕽𝖎𝖛𝖊𝖓𝖉𝖊𝖑𝖑, the winning submission to Hyprland's 4th ricing competition: https://codeberg.org/zacoons/rivendell-hyprdots

All credit to zacoons, I've only forked and modified.

# Installation

Build from source:

```
% git clone https://github.com/JAGdeRoos/imgborders
% cmake -B build
% cmake --build build
```

Then add it to Hyprland:

```
% hyprctl plugin load /home/$USER/dev/imgborders/build/imgborders.so
```

To remove it just replace `load` with `unload` in the above.

# Configuration

## My Config

``` conf
imgborders {
         enabled = true
         image = ~/.config/assets/imgborder_techy png
         sizes = 21,32,15,21 # left, right, top, bottom 
         insets = 1,2,5,1 # left, right, top, bottom

         horsizes = 6, 14, 11, 5
         versizes = 12, 12, 15, 8
         
         scale = 1
         smooth = true
         blur = false

         topplacements = 25,75
         bottomplacements = 45,55
         leftplacements = 30,70
         rightplacements = 20,80
     }
```

I don't think I need to explain `enabled` or `image`.

`sizes` - (4 integers) Defines the number of pixels from each edge of the image to take.

`insets` - (4 integers) Defines the amount by which to inset each side into the window.

`horsizes/versizes` - (4 integers) Defines the number of pixels for each additional piece of edge, first two count from top, second two count from bottom.

`scale` - Scale the borders by some amount.

`smooth` - Whether the image pixels should have smoothing (true) or if it should be pixelated (false).

`blur` - Whether transparency should have blur (true) or if it should be clear (false).

`side-placements` - (2 integers) Defines where along the edge to place the custom parts for each side.

## Window rules

`plugin:imgborders:noimgborders` - Disables image borders.


### How It Works

Each edge is divided into **7 sections**:
- **Corner** (static)
- **Edge section 1** (tiling) 
- **Custom section 1** (static, positioned)
- **Middle edge** (tiling)
- **Custom section 2** (static, positioned) 
- **Edge section 2** (tiling)
- **Corner** (static)

### Section Parameters

**`horsizes`** - (4 integers) Pixel widths from source image for horizontal edge sections:
- `[0]` = left edge section 1 width
- `[1]` = left custom section width  
- `[2]` = right custom section width
- `[3]` = right edge section 2 width

**`versizes`** - (4 integers) Pixel heights from source image for vertical edge sections:
- `[0]` = top edge section 1 height
- `[1]` = top custom section height
- `[2]` = bottom custom section height  
- `[3]` = bottom edge section 2 height

Note this means the middle edge for both the vertical and horizontal edge is populated by the space between defined sections.

**Note:** All four placement parameters are required for the 7-section border system to work.
