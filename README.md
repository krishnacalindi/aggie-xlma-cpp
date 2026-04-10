# Aggie XLMA (C++)

A desktop application written in C++ that helps you view, analyze, and interact with LMA ([Lightning Mapping Array](https://lightning.tamu.edu/hlma/)) data. This project is a long-term effort to modernize the existing XLMA application. Our mission is to provide a reliable and fast tool for analysis of lightning data. Plase check the releases tab for the latest version!

## Screenshot:
![GUI](./extra/screenshot.png)

## Performance:
All tests were conducted with files from the July 12, 2022 TRACER case from the HLMA. For each row, tests were repeated 5 times, and average values are shown below. 
- Load time: Time for initial `DuckDb` file loading
- Render time: Time for initial filter, populating VBO and final `OpenGL` render
### Test PC Specs:
- Model: Legion Pro 5 16ARX8 (laptop)
- CPU: AMD Ryzen 9 7945 HX w/ Radeon graphics 
- GPU: NVIDIA GeForce RTX 4070 Laptop GPU
- RAM: 32 GB

| # Files | Combined size (MB) | # VHF sources | Load time (ms) | Render time (ms) | Total time (ms) |
| :----- | :----- | :------ | :----- | :----- | :----- |
| 56 | 5.1 | 17,408 | 49.6 | 25.8 | 75.4 |
| 75 | 10.2 | 46,648 | 68.6 | 79.0 | 147.6 |
| 104 | 25.4 | 161,267 | 110.4 | 85.2 | 195.6 |
| 118 | 50.4 | 352,814 | 223.2 | 152.6 | 375.8 |
| 123 | 76.0 | 568,431 | 291.2 | 166.4 | 457.6 |
| 127 | 102.8 | 810,450 | 361.0 | 191.0 | 552.0 |
| 133 | 156.7 | 1,284,746 | 507.0 | 246.8 | 753.8 |
| 137 | 207.4 | 1,754,070 | 646.6 | 296.6 | 943.2 |
| 142 | 284.4 | 2,438,552 | 754.0 | 394.8 | 1148.8 |
| 144 | 323.5 | 2,775,961 | 839.0 | 500.0 | 1339.0 |

## Features:

- Blazing fast performance powered by `DuckDB` in-memory queries and `OpenGL` rendering.
- Load and visualize ENTLN/NLDN lightning data alongside VHF sources, with toggleable IC and CG strokes.
- Interactive polygon selection with preview of selection boundary
- Multiple themes, colormaps, and color-by options available.
- Export current data as `.parquet` or `.csv`, and save plots as `.png` or animated `.

## Authors:
- Krishna Calindi
- Isaac Jones
- [Dr.Timothy Logan](https://artsci.tamu.edu/atmos-science/contact/profiles/timothy-logan.html)

## References:
McCaul , E. W., S. J. Goodman, K. M. LaCasse, and D. J. Cecil, 2009: Forecasting Lightning Threat Using Cloud-Resolving Model Simulations. Wea. Forecasting, 24, 709–729, https://doi.org/10.1175/2008WAF2222152.1.

gif.h: Obtained from [here](https://github.com/charlietangora/gif-h)
