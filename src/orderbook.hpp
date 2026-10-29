#include <compare>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <map>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>

// class to store prices or quantities of BTC
//
// numbers are represented in floating point format. they are too
// large to store in a double, however, they can be quantised to fit into a
// uint64_t
//
// since these numbers have fixed precision, we can set the precision using a
// template paremeter
template <int precision>
struct Number {
    uint64_t val;
    static constexpr uint64_t scale = []() {
        uint64_t r = 1;
        for (int i = 0; i < precision; i++)
            r *= 10;
        return r;
    }();

    Number(std::string_view str) : val()
    {
        // make sure that there are `precision` decimal places
        if (int i = str.size() - 1 - precision;
            i < 0 || i >= str.size() || str[i] != '.')
            goto err;
        for (char c : str) {
            if (c == '.')
                continue;
            if (c < '0' || c > '9')
                goto err;
            val *= 10;
            val += c - '0';
        }
        return;
    err:
        throw std::invalid_argument(std::format("invalid number: {}", str));
    }

    uint64_t integral() const
    {
        return val / scale;
    }

    uint64_t fractional() const
    {
        return val % scale;
    }

    std::strong_ordering operator<=>(const Number &o) const = default;
};

// print to stream
template <int precision>
std::ostream &operator<<(std::ostream &os, const Number<precision> &num)
{
    return os << num.integral() << '.' << std::setfill('0') << std::setw(precision) << num.fractional();
}

// hash (needed for std::unordered_map)
template <int precision>
struct std::hash<Number<precision>> {
    uint64_t operator()(const Number<precision> &n) const
    {
        return std::hash<uint64_t> { }(n.val);
    }
};

using Price = Number<5>;
using Quantity = Number<10>;

// class to represent a half of the orderbook
//
// - getting the top price level, updating a price's quantity or removing an
// order takes constant time
// - inserting a new price level takes logarithmic time
template <typename Compare>
class OrderList {
    std::map<Price, Quantity, Compare> levels;
    // this hashmap is used to look up price-quantity pair in the levels map in
    // constant time
    using Iterator = decltype(levels.begin());
    std::unordered_map<Price, Iterator> orders;

public:
    void clear()
    {
        levels.clear();
        orders.clear();
    }

    void add_order(Price price, Quantity quantity)
    {
#if 0
    if (quantity.val > 0)
      levels.insert_or_assign(price, quantity);
    else
      levels.erase(price);
#else
        if (auto it = orders.find(price); it != orders.end()) {
            if (quantity.val > 0)
                // if the quantity is greater than 0, update the quantity for the given
                // price level
                it->second->second = quantity;
            else {
                // otherwise remove the price's entry from both maps
                levels.erase(it->second);
                orders.erase(it);
            };
        } else {
            if (quantity.val > 0) {
                // this price is not in the order book, so insert it
                auto [it, ok] = levels.emplace(price, quantity);
                if (!ok)
                    std::unreachable();
                orders[price] = it;
            }
        }
#endif
    }

    std::optional<std::pair<Price, Quantity>> get_top_order()
    {
        if (levels.empty())
            return std::nullopt;
        else
            return *levels.rbegin();
    }
};

class OrderBook {
    OrderList<std::less<Price>> bids;
    OrderList<std::greater<Price>> asks;
    // used to knokw if the order book is synchronised with the exchange's
    uint64_t first_update = 0, last_update = 0;

public:
    void clear()
    {
        bids.clear();
        asks.clear();
    }

    // when Gemini sends a snapshot of the orderbook, the first and last update
    // IDs are equal, we can use this information to know whether stream data is
    // an initial snapshot or a differential update
    bool set_updates(uint64_t new_first_update, uint64_t new_last_update)
    {
        // if the update is a snapshot of the orderbook, clear any existing orders
        if (new_first_update == new_last_update) {
            bids.clear();
            asks.clear();
        }
        // this checks if we are out of sync
        else if (last_update && first_update && last_update != new_first_update)
            return false;
        first_update = new_first_update;
        last_update = new_last_update;
        return true;
    }

    void add_order(char side, Price price, Quantity quantity)
    {
        if (side == 'b')
            bids.add_order(price, quantity);
        else
            asks.add_order(price, quantity);
    }

    auto get_top_bid()
    {
        return bids.get_top_order();
    }

    auto get_top_ask()
    {
        return asks.get_top_order();
    }

    void print_top_bid_and_ask()
    {
        auto bid = get_top_bid();
        auto ask = get_top_ask();
        if (!bid || !ask)
            return;
        using T = std::tuple<uint64_t, uint64_t, uint64_t, uint64_t>;
        static T last;
        T cur { bid->first.val, bid->second.val, ask->first.val, ask->second.val };
        if (cur == last)
            return;
        std::cout << bid->first << ' ' << bid->second << " - " << ask->first << ' ' << ask->second << std::endl;
        last = cur;
    }
};
