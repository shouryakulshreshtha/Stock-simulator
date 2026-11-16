# StockSim: live stock exchange simulator (UNC302 OOP project)

C++17 console exchange. Bot traders send buy and sell orders, a matching engine pairs them by
price-time priority, and the market price follows real historical prices (Kaggle NIFTY-50 CSV).
A terminal dashboard and a browser chart show everything live.

Team: Shourya Kulshreshtha, Krish Khosla.

## Build and run

Linux / macOS / WSL / MSYS2:

    make              # builds ./stocksim
    make test         # unit tests (33 tests)
    ./stocksim

Windows without make (MinGW-w64 with POSIX threads, for example MSYS2 UCRT64):

    g++ -std=c++17 -O2 -pthread src/*.cpp -o stocksim.exe
    stocksim.exe

Use Windows Terminal (or any terminal with ANSI support) and make the window large: **at least 120 columns
and 36 rows**. The screen redraws in place and keeps your typing line at the bottom.

Open `web/chart.html` in a browser while the program runs. It reloads `web/chart_data.js` every second.

## Data

Put Kaggle files in `data/` as `RELIANCE.csv`, `TCS.csv`, `HDFCBANK.csv` (see `data/README.txt`).
Missing file: that stock uses a generated random-walk price and the program prints a note.
Kaggle files contain prices only, never orders: the orders come from the bots.

## What you can type while it runs

    BUY  RELIANCE 10 2450       limit order
    SELL TCS 5 MKT              market order
    CANCEL RELIANCE 1234        cancel your own order (the id is shown when you send it)
    QUIT

Your account is called YOU. Orders outside +-20% of the reference price, or that you cannot pay for, are rejected.

## Options

    ./stocksim --help
    ./stocksim --headless --no-ansi --duration 20      # no keyboard, prints a final report (good for logs)
    ./stocksim --step 100 --steps-per-day 10           # faster replay (default 250 ms x 20 = 5 s per trading day)
    ./stocksim --symbols RELIANCE,TCS,INFY

## Demo script for the lab

1. Start `./stocksim`, open `web/chart.html`. The market is already moving: bots trade, the price follows the data.
2. Point at the order book (top 5 bids and asks) and the trade feed.
3. Type `BUY RELIANCE 10 MKT` and show it matching against a bot order (see "events" and the portfolio line for YOU).
4. Type `BUY TCS 10 99999` to show the risk check rejecting it.
5. Type `QUIT` and show the final report: books consistent, `live Order objects after shutdown: 0`.
6. Open `events.csv`: every order, cancel, reject and trade is logged.

## Files

| File | What it holds |
| --- | --- |
| Order.h/.cpp | abstract Order, LimitOrder, MarketOrder; overloaded new/delete, `<<`, `<` |
| Trade.h/.cpp | one match; friend `<<`; copy and move constructors |
| OrderBook.h/.cpp | bids and asks of one stock, price-time matching, cancel, self-trade prevention, invariant check |
| MatchingEngine.h/.cpp | one book per symbol, engine thread, overloaded `submit()`, Observer notifications |
| RiskManager.h/.cpp | price band and cash/share check before an order enters the book |
| Portfolio.h/.cpp | cash, shares, reservations, P&L; `+=` with a Trade; copy/move constructors; is a TradeListener |
| TradeListener.h | Observer interface (Dashboard, EventLogger, ChartExporter, Portfolio implement it) |
| ThreadSafeQueue.h | class template used between trader threads and the engine |
| Participant.h/.cpp | virtual base Participant; Trader, Auditable; multiple inheritance in BotTrader |
| Bots.h/.cpp | RandomBot, MomentumBot, MarketMakerBot |
| ManualTrader.h/.cpp | keyboard trader |
| MarketDataFeed.h/.cpp | PriceSource, CsvPriceSource, GeneratedPriceSource, replay thread; overloaded `>>` for a price row |
| MarketState.h/.cpp | shared prices (reference, last trade, volume) |
| Dashboard.h/.cpp | terminal screen using stream manipulators; overloaded `show()` |
| EventLogger.h/.cpp | events.csv (ofstream, RAII) |
| ChartExporter.h/.cpp | writes web/chart_data.js for the browser chart |
| Utils.h, Errors.h, Types.h, Command.h | function and class templates, exception classes, basic types |
| main.cpp | builds everything, starts and stops the threads |
| tests/test_main.cpp | 33 unit tests, including a 20,000-order random stress run |

## Checked

- 33 tests, about 4,400 checks, all passing (`make test`).
- Tests also pass under AddressSanitizer and UndefinedBehaviorSanitizer.
- A 15-second multi-threaded run under ThreadSanitizer reported no data races.
- Stress run: books stay sorted and uncrossed, total cash and total shares are conserved, nothing is reserved twice.

## Known simplifications (say these in the viva before sir finds them)

- One trading day of the CSV is replayed over 20 steps; the prices between are interpolated plus noise.
- No fees, no short selling, no order amendment, no crash recovery from the log.
- P&L includes the price change of shares a trader holds, not only trading profit.
