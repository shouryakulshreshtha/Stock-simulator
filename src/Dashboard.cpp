#include "Dashboard.h"

#include <iomanip>
#include <sstream>

#include "Utils.h"

namespace {

const char* const RESET = "\033[0m";
const char* const GREEN = "\033[32m";
const char* const RED = "\033[31m";
const char* const BOLD = "\033[1m";
const char* const DIM = "\033[2m";
const char* const YELLOW = "\033[33m";

// Colour is applied AFTER padding, so the invisible escape codes never disturb the column widths.
std::string paint(bool ansi, const char* color, const std::string& text) {
    return ansi ? std::string(color) + text + RESET : text;
}

std::string fit(std::string text, std::size_t width) {
    if (text.size() > width) text.resize(width);
    text.append(width - text.size(), ' ');
    return text;
}

std::string withCommas(long long value) {
    const bool negative = value < 0;
    std::string digits = std::to_string(negative ? -value : value);
    for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3) digits.insert(static_cast<std::size_t>(i), ",");
    return negative ? "-" + digits : digits;
}

std::string clock(std::chrono::seconds total) {
    std::ostringstream os;
    os << std::setfill('0') << std::setw(2) << total.count() / 3600 << ':' << std::setw(2) << (total.count() / 60) % 60 << ':'
       << std::setw(2) << total.count() % 60;
    return os.str();
}

// Bots create hundreds of cancels and self-trade cancels; only rejects are worth a line on the screen.
// Everything the person at the keyboard does is shown, whatever its type.
bool isNotable(const std::string& type) { return type == "REJECT"; }

}  // namespace

Dashboard::Dashboard(const MatchingEngine& engine, const MarketState& market, const RiskManager& risk, std::string dataLine)
    : engine_(engine),
      market_(market),
      risk_(risk),
      dataLine_(std::move(dataLine)),
      start_(std::chrono::steady_clock::now()),
      lastRender_(start_) {}

void Dashboard::addAuditable(const Auditable* trader) { auditables_[trader->id()] = trader; }

void Dashboard::setStatusSource(std::function<std::string()> source) { statusSource_ = std::move(source); }

// ---------------------------------------------------------------- observer callbacks

void Dashboard::onTrade(const Trade& trade) {
    std::lock_guard<std::mutex> lock(mutex_);
    trades_.push_back(show(trade));
    if (trades_.size() > 12) trades_.pop_front();
}

void Dashboard::onOrderEvent(const OrderEvent& event) {
    const bool mine = event.trader == "YOU";
    if (!isNotable(event.type) && !mine) return;

    std::ostringstream os;
    os << timeStamp() << "  " << std::left << std::setw(11) << event.type << ' ' << event.symbol;
    if (event.orderId != 0) os << " #" << event.orderId;
    if (!event.trader.empty()) os << ' ' << event.trader;
    if (!event.detail.empty()) os << " - " << event.detail;

    std::lock_guard<std::mutex> lock(mutex_);
    events_.push_back(os.str());
    if (events_.size() > 8) events_.pop_front();
}

// ---------------------------------------------------------------- formatting (overloaded show)

std::string Dashboard::show(const Trade& trade) {
    std::ostringstream os;
    os << std::left << std::setw(9) << trade.timeString() << std::setw(11) << trade.symbol() << std::right << std::setw(5)
       << trade.quantity() << std::setw(10) << std::fixed << std::setprecision(2) << toRupees(trade.price()) << "  "
       << trade.buyer() << " > " << trade.seller();
    return os.str();
}

std::string Dashboard::show(const Portfolio::Summary& s, long commandsSent) {
    std::ostringstream os;
    os << std::left << std::setw(13) << s.owner << std::right << std::setw(15) << withCommas(s.cash / 100) << std::setw(15)
       << withCommas(s.holdingsValue / 100) << std::setw(15) << withCommas(s.equity / 100) << std::setw(13)
       << withCommas(s.pnl / 100) << std::setw(10) << commandsSent;
    return os.str();
}

// ---------------------------------------------------------------- the frame

std::string Dashboard::render(bool ansi) {
    std::vector<std::string> lines;
    const std::vector<std::string> symbols = market_.symbols();
    const std::map<std::string, Paise> marks = market_.marks();
    const MatchingEngine::Stats stats = engine_.stats();

    // Orders per second since the previous frame.
    const auto now = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(now - lastRender_).count();
    if (seconds > 0.3) {
        ordersPerSecond_ = static_cast<double>(stats.orders - lastOrders_) / seconds;
        lastOrders_ = stats.orders;
        lastRender_ = now;
    }
    const auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_);

    // ---- title
    {
        std::ostringstream os;
        os << " STOCKSIM  live exchange simulator    replay day: " << market_.dateLabel() << "    uptime " << clock(uptime)
           << "    orders/s " << std::fixed << std::setprecision(1) << ordersPerSecond_
           << "    live Order objects: " << Order::liveOrders();
        lines.push_back(paint(ansi, BOLD, os.str()));
        lines.push_back(paint(ansi, DIM, std::string(118, '-')));
    }

    // ---- market table
    {
        std::ostringstream h;
        h << ' ' << fit("SYMBOL", 11) << std::right << std::setw(11) << "REF" << std::setw(11) << "LAST" << std::setw(9) << "CHG%"
          << std::setw(11) << "VOLUME" << std::setw(9) << "TRADES" << std::setw(11) << "BID" << std::setw(11) << "ASK"
          << std::setw(9) << "SPREAD";
        lines.push_back(paint(ansi, DIM, h.str()));

        int rows = 0;
        for (const std::string& symbol : symbols) {
            if (rows++ >= 4) break;
            const BookSnapshot top = engine_.snapshot(symbol, 1);
            const Paise bid = top.bids.empty() ? 0 : top.bids.front().price;
            const Paise ask = top.asks.empty() ? 0 : top.asks.front().price;
            const Paise last = market_.lastTrade(symbol);
            const Paise open = market_.openPrice(symbol);
            const double change = open > 0 ? (static_cast<double>(last) / static_cast<double>(open) - 1.0) * 100.0 : 0.0;

            std::ostringstream r;
            r << ' ' << std::left << std::setw(11) << symbol << std::right << std::fixed << std::setprecision(2) << std::setw(11)
              << toRupees(market_.reference(symbol)) << std::setw(11) << toRupees(last);
            std::ostringstream c;
            c << std::showpos << std::fixed << std::setprecision(2) << change << '%';
            std::string changeCell = c.str();
            if (changeCell.size() < 9) changeCell.insert(0, 9 - changeCell.size(), ' ');
            const std::string changeText = paint(ansi, change >= 0 ? GREEN : RED, changeCell);
            std::ostringstream tail;
            tail << std::right << std::fixed << std::setprecision(2) << std::setw(11) << withCommas(market_.volume(symbol)) << std::setw(9)
                 << market_.tradeCount(symbol) << std::setw(11) << (bid ? formatPrice(bid) : "-") << std::setw(11)
                 << (ask ? formatPrice(ask) : "-") << std::setw(9) << ((bid && ask) ? formatPrice(ask - bid) : "-");
            lines.push_back(r.str() + changeText + tail.str());
        }
        while (rows++ < 4) lines.push_back("");
    }
    lines.push_back("");

    // ---- order book (left) next to recent trades (right)
    std::vector<std::string> trades;
    std::vector<std::string> events;
    {
        std::lock_guard<std::mutex> lock(mutex_);  // copy and release at once: never hold it while calling the engine
        trades.assign(trades_.begin(), trades_.end());
        events.assign(events_.begin(), events_.end());
    }

    std::vector<std::string> left;
    if (!symbols.empty()) {
        const std::size_t focus = static_cast<std::size_t>(uptime.count() / 6) % symbols.size();  // rotates every 6 s
        const BookSnapshot book = engine_.snapshot(symbols[focus], 5);
        left.push_back(paint(ansi, BOLD, "ORDER BOOK  " + book.symbol + "  (top 5, rotates)"));
        std::ostringstream hdr;
        hdr << std::right << std::setw(9) << "BID QTY" << std::setw(11) << "BID PX" << " | " << std::setw(11) << "ASK PX" << std::setw(9) << "ASK QTY";
        left.push_back(paint(ansi, DIM, hdr.str()));
        for (std::size_t i = 0; i < 5; ++i) {
            std::ostringstream row;
            row << std::right;
            if (i < book.bids.size())
                row << paint(ansi, GREEN, [&] { std::ostringstream c; c << std::setw(9) << book.bids[i].quantity << std::setw(11) << formatPrice(book.bids[i].price); return c.str(); }());
            else
                row << std::string(20, ' ');
            row << " | ";
            if (i < book.asks.size())
                row << paint(ansi, RED, [&] { std::ostringstream c; c << std::setw(11) << formatPrice(book.asks[i].price) << std::setw(9) << book.asks[i].quantity; return c.str(); }());
            left.push_back(row.str());
        }
    }

    std::vector<std::string> right;
    right.push_back(paint(ansi, BOLD, "RECENT TRADES"));
    right.push_back(paint(ansi, DIM, fit("time", 9) + fit("symbol", 11) + "  qty     price  buyer > seller"));
    const std::size_t shown = std::min<std::size_t>(5, trades.size());  // the 5 newest, oldest of them first
    for (std::size_t i = 0; i < shown; ++i) right.push_back(fit(trades[trades.size() - shown + i], 62));
    if (trades.empty()) right.push_back(paint(ansi, DIM, "waiting for the first trade..."));

    for (std::size_t i = 0; i < 7; ++i) {
        std::string l = i < left.size() ? left[i] : std::string();
        // pad the left panel to a fixed visible width (50) even when it contains colour codes
        std::size_t visible = 0;
        for (std::size_t k = 0; k < l.size(); ++k) {
            if (l[k] == '\033') {
                while (k < l.size() && l[k] != 'm') ++k;
            } else {
                ++visible;
            }
        }
        if (visible < 50) l.append(50 - visible, ' ');
        lines.push_back(" " + l + "   " + (i < right.size() ? right[i] : std::string()));
    }
    lines.push_back("");

    // ---- portfolios
    {
        std::ostringstream h;
        h << ' ' << std::left << std::setw(13) << "TRADER" << std::right << std::setw(15) << "CASH (Rs)" << std::setw(15)
          << "SHARES (Rs)" << std::setw(15) << "EQUITY (Rs)" << std::setw(13) << "P&L (Rs)" << std::setw(10) << "SENT";
        lines.push_back(paint(ansi, DIM, h.str()));
        int rows = 0;
        for (Portfolio* portfolio : risk_.portfolios()) {
            if (rows++ >= 5) break;
            const Portfolio::Summary summary = portfolio->summary(marks);
            auto audit = auditables_.find(summary.owner);
            const long sent = audit == auditables_.end() ? 0 : audit->second->sent();
            lines.push_back(" " + paint(ansi, summary.pnl >= 0 ? GREEN : RED, show(summary, sent)));
        }
    }

    // ---- recent notable events, stats, data, manual trader
    lines.push_back(paint(ansi, DIM, " events (rejected orders, and everything you do)"));
    const std::size_t eventsShown = std::min<std::size_t>(3, events.size());  // the 3 newest
    for (std::size_t i = 0; i < 3; ++i)
        lines.push_back(i < eventsShown ? " " + paint(ansi, YELLOW, fit(events[events.size() - eventsShown + i], 110)) : std::string());
    {
        std::ostringstream os;
        os << " orders accepted " << stats.orders << "   trades " << stats.trades << "   rejected " << stats.rejected
           << "   cancels " << stats.cancels;
        lines.push_back(paint(ansi, BOLD, os.str()));
    }
    lines.push_back(paint(ansi, DIM, " data: " + dataLine_));
    lines.push_back(" last command: " + (statusSource_ ? statusSource_() : std::string("(headless: no keyboard trader)")));
    lines.push_back(paint(ansi, DIM, " commands: BUY RELIANCE 10 2450 | SELL TCS 5 MKT | CANCEL RELIANCE 1234 | QUIT"));

    lines.resize(kFrameLines);  // exactly kFrameLines lines, so the frame never moves

    std::ostringstream out;
    if (ansi) out << "\033" "7" << "\033[H";  // save the cursor (it sits in the prompt), go to the top left
    for (const std::string& line : lines) {
        out << line;
        if (ansi) out << "\033[K";  // erase what was left of the previous frame on this line
        out << '\n';
    }
    if (ansi) out << "\033" "8";  // put the cursor back where the user is typing
    return out.str();
}
