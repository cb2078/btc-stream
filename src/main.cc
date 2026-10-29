#include <csignal>
#include <cstdint>
#include <print>
#include <utility>

#include <cinatra.hpp>
#include <simdjson.h>

#include "orderbook.hpp"

#define MESSAGE_CODE_EMPTY -2
#define MESSAGE_CODE_FAIL -1
#define MESSAGE_CODE_STREAM 0
#define MESSAGE_CODE_RESPONSE

// globals
const char *symbol = "btcusd@depth@100ms";
const char *uri = "wss://ws.gemini.com?snapshot=-1";
simdjson::ondemand::parser parser;
cinatra::coro_http_client client;
OrderBook orderbook;
std::unordered_map<int, async_simple::Promise<std::string>> pending;
std::atomic<bool> running = true;

void signal_handler(int signal)
{
    if (signal == SIGINT)
        running = false;
    else
        std::unreachable();
}

bool update_orderbook(std::string_view str)
{
    // std::cout << str << std::endl;
    simdjson::padded_string json(str);
    auto doc = parser.iterate(json);
    try {
        // if we have missed a depth update, return false
        if (!orderbook.set_updates(doc["U"], doc["u"]))
            return false;
        // update the bid and ask orders
        for (std::string_view side : { "b", "a" })
            for (simdjson::ondemand::array arr : doc[side]) {
                std::string_view str;
                auto it = arr.begin();
                str = *it;
                Price price(str);
                ++it;
                str = *it;
                Quantity quantity(str);
                orderbook.add_order(side[0], price, quantity);
            }
        return true;
    } catch (simdjson::simdjson_error &e) {
        std::cerr << "JSON Exception: " << e.what() << " near " << doc.current_location() << " in " << str << std::endl;
        throw;
    }
}

// handle an incoming websocket message
//
// - if it is valid, decide whether it is stream data or a response from a
// request
// - if it is invalid, raise an error
// - if it is empty, do nothing
int handle_message(cinatra::resp_data data)
{
    // handle network error
    if (data.net_err) {
        std::println(std::cerr, "Network Error: {}", data.net_err.message());
        return MESSAGE_CODE_FAIL;
    }

    // sometimes Gemini sends a UUID, skip this and don't do anything
    if (data.resp_body.size() > 0 && data.resp_body[0] != '{')
        return MESSAGE_CODE_EMPTY;

    // handle json
    simdjson::padded_string json(data.resp_body);
    auto doc = parser.iterate(json);
    try {
        // look at the first field of the JSON object to decide whether it is a
        // stream or a response
        auto obj = doc.get_object();
        auto it = obj.begin();
        if (it != obj.end()) {
            auto field = (*it);
            std::string_view key = field.escaped_key().value();
            // the first field in a response is "id"
            if (key == "id") {
                int id = field.value().get_int64_in_string();
                if ((*++it).value().get_int64() == 200)
                    return id;
                // if the request was invliad, show the error response
                std::println(std::cerr, "Error Response: code: {}, msg: \"{}\"", (int64_t)doc["error"]["code"], std::string_view(doc["error"]["msg"]));
                return MESSAGE_CODE_FAIL;
                // the first field in a stream id "e"
            } else if (key == "e")
                return MESSAGE_CODE_STREAM;
        }
    } catch (simdjson::simdjson_error &e) {
        std::cerr << "JSON Exception: " << e.what() << " near "
                  << doc.current_location() << " in " << data.resp_body
                  << std::endl;
        throw;
    }
    // if we couldn't figure out whether it is a stream or response, raise an
    // error
    std::println(std::cerr, "Unknown JSON format: {}", data.resp_body);
    return MESSAGE_CODE_FAIL;
}

// wrapper for writing websockets
async_simple::coro::Lazy<std::string> write_websocket(std::string method, std::string params)
{
    // use a unique id for each request, so every response can be matched to its
    // request
    static int total = 1;
    int id = total++;

    // create a promise that the reader loop will fullfill
    async_simple::Promise<std::string> promise;
    auto future = promise.getFuture();
    pending[id] = std::move(promise);

    constexpr std::string_view fmt = R"({{
      "id": "{}",
      "method": "{}",
      "params": {}
    }})";
    std::string data = std::format(fmt, id, method, params);
    co_await client.write_websocket(data);
    co_return co_await std::move(future);
}

async_simple::coro::Lazy<std::string> subscribe(std::string stream)
{
    return write_websocket("subscribe", std::format("[\"{}\"]", stream));
}

async_simple::coro::Lazy<std::string> unsubscribe(std::string stream)
{
    return write_websocket("unsubscribe", std::format("[\"{}\"]", stream));
}

// this function continuously reads websockets
//
// - if it is a response from a request, notify the sender
// - if it is stream data, update the order book
//
// it will gracefully exit once Ctrl-C is pressed
async_simple::coro::Lazy<void> reader_loop()
{
    while (running) {
        auto data = co_await client.read_websocket();
        int result = handle_message(data);
        switch (result) {
            case MESSAGE_CODE_EMPTY:
                break;
            case MESSAGE_CODE_FAIL:
                co_return;
            case MESSAGE_CODE_STREAM:
                if (update_orderbook(data.resp_body))
                    orderbook.print_top_bid_and_ask();
                else
                    // if we were not able to update the order book (because they stream
                    // data is out of sync), request a new snapshot of the orderbook by
                    // resubscribing to the feed
                    //
                    // NOTE this can't be done with `co_await unsubscribe(...)` because it
                    // would cause a deadlock

                    // clang-format off
                    []() -> async_simple::coro::Lazy<void> {
                        co_await unsubscribe(symbol);
                        co_await subscribe(symbol);
                    }().start([](auto &&) -> void {
                    });
                // clang-format on
                break;
            default:
                auto it = pending.find(result);
                auto promise = std::move(it->second);
                // remove the request id from the pending map since it is no longer needed
                pending.erase(it);
                // notify the sender that their request has been handled by updating the
                // promise
                promise.setValue(data.resp_body);
                break;
        }
    }
}

int main()
{
    // exit the program gracefully when Ctrl-C is pressed
    std::signal(SIGINT, signal_handler);

    async_simple::coro::syncAwait([]() -> async_simple::coro::Lazy<void> {
        if (auto data = co_await client.connect(uri); data.net_err) {
            std::println(std::cerr, "Network Error: {}", data.net_err.message());
            co_return;
        }

        // start the reader loop
        async_simple::Promise<async_simple::Unit> promise;
        auto future = promise.getFuture();
        reader_loop().start([&promise](async_simple::Try<void> &&result) -> void {
            if (result.hasError())
                std::println("'reader_loop finished with error");
            promise.setValue(async_simple::Unit { });
        });

        // send the initial subscribe request
        co_await subscribe(symbol);

        // wait for the reader loop to finish
        co_await std::move(future);
    }());
    std::println(std::cerr, "OK");
    return 0;
}
