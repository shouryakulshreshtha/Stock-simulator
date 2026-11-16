Put the Kaggle price files here, one per stock, named <SYMBOL>.csv:

    data/RELIANCE.csv
    data/TCS.csv
    data/HDFCBANK.csv

Dataset used: "NIFTY-50 Stock Market Data (2000 - 2021)" on Kaggle (NSE India daily prices).
Download it from Kaggle (login needed), copy the three stock files here and rename them if the names differ.

The loader finds the columns by name (Date, Close, and optionally Open, High, Low, Volume), so the column order
does not matter. Rows with an empty or invalid Close are skipped and counted.

If a file is missing, StockSim prints a note and uses a generated random-walk price for that stock,
so the program always runs.

sample/SAMPLE_kaggle_format.csv is SYNTHETIC test data in the same column layout. It is NOT real market data.
