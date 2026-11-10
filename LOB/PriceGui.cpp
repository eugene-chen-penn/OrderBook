#include "PriceGui.hpp"

#include <arpa/inet.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

extern char** environ;

namespace {

constexpr std::string_view page = R"html(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>OrderBook Price</title>
<style>
  * { box-sizing: border-box; }
  body { margin: 0; background: #0b1020; color: #e7ecf5; font: 14px system-ui; }
  main { max-width: 1400px; margin: auto; padding: 24px; }
  h1 { margin: 0 0 18px; font-size: 22px; }
  .cards { display: flex; gap: 16px; margin-bottom: 16px; }
  .card { flex: 1; padding: 16px; background: #151d31; border: 1px solid #25304a;
          border-radius: 10px; }
  .label { color: #8f9bb3; font-size: 12px; text-transform: uppercase; }
  .value { margin-top: 6px; font: 600 28px ui-monospace, monospace; }
  .book { max-width: 620px; margin: 16px auto; }
  .book h2 { margin: 0 0 10px; font-size: 14px; }
  table { width: 100%; border-collapse: collapse; font: 14px ui-monospace, monospace; }
  th, td { padding: 7px 8px; border-top: 1px solid #25304a; text-align: right; }
  th { color: #8f9bb3; font: 11px system-ui; text-transform: uppercase; }
  .bid { color: #55d6be; }
  .ask { color: #ff7085; }
  .spread td { padding: 5px; color: #8f9bb3; text-align: center; background: #0b1020; }
  canvas { width: 100%; height: 560px; background: #11182a; border: 1px solid #25304a;
           border-radius: 10px; }
  #status { margin-top: 8px; color: #8f9bb3; }
</style>
</head>
<body><main>
  <h1>OrderBook — all samples since start</h1>
  <div class="cards">
    <div class="card"><div class="label">Latest close</div><div id="price" class="value">—</div></div>
    <div class="card"><div class="label">Quantity / 100 ms</div><div id="quantity" class="value">—</div></div>
  </div>
  <section class="card book">
    <h2>Market depth — asks above, bids below</h2>
    <table>
      <thead><tr><th>Side</th><th>Price</th><th>Quantity</th></tr></thead>
      <tbody id="asks"></tbody>
      <tbody><tr class="spread"><td colspan="3">spread</td></tr></tbody>
      <tbody id="bids"></tbody>
    </table>
  </section>
  <canvas id="chart"></canvas>
  <div id="status">Waiting for the first trade…</div>
</main>
<script>
const samples = [];
const PRICE_SCALE = 10000;
const canvas = document.getElementById('chart');
const context = canvas.getContext('2d');

function resize() {
  const ratio = window.devicePixelRatio || 1;
  canvas.width = Math.floor(canvas.clientWidth * ratio);
  canvas.height = Math.floor(canvas.clientHeight * ratio);
  context.setTransform(ratio, 0, 0, ratio, 0, 0);
  draw();
}

function draw() {
  const width = canvas.clientWidth;
  const height = canvas.clientHeight;
  context.clearRect(0, 0, width, height);
  if (!samples.length) return;

  const left = 70, right = 18, top = 22, bottom = 42;
  const priceHeight = Math.floor(height * 0.62);
  const quantityTop = priceHeight + 42;
  const plotWidth = width - left - right;
  let minimumPrice = samples[0].close;
  let maximumPrice = samples[0].close;
  let maximumQuantity = 0;
  for (const sample of samples) {
    minimumPrice = Math.min(minimumPrice, sample.close);
    maximumPrice = Math.max(maximumPrice, sample.close);
    maximumQuantity = Math.max(maximumQuantity, sample.quantity);
  }
  const priceRange = maximumPrice - minimumPrice || 1;
  const timeRange = samples[samples.length - 1].time_ms - samples[0].time_ms || 1;

  context.strokeStyle = '#26324d';
  context.lineWidth = 1;
  context.beginPath();
  context.moveTo(left, top);
  context.lineTo(left, height - bottom);
  context.lineTo(width - right, height - bottom);
  context.stroke();

  context.fillStyle = '#8f9bb3';
  context.font = '12px ui-monospace, monospace';
  context.fillText((maximumPrice / PRICE_SCALE).toFixed(2), 4, top + 5);
  context.fillText((minimumPrice / PRICE_SCALE).toFixed(2), 4, priceHeight + 5);
  context.fillText('close', left, 15);
  context.fillText('quantity / 100 ms', left, quantityTop - 10);

  context.strokeStyle = '#55d6be';
  context.lineWidth = 2;
  context.beginPath();
  samples.forEach((sample, index) => {
    const x = left + ((sample.time_ms - samples[0].time_ms) / timeRange) * plotWidth;
    const y = top + ((maximumPrice - sample.close) / priceRange) * (priceHeight - top);
    if (index === 0) context.moveTo(x, y); else context.lineTo(x, y);
  });
  context.stroke();

  context.fillStyle = '#6c8cff';
  const barWidth = Math.max(1, plotWidth / samples.length);
  for (const sample of samples) {
    const x = left + ((sample.time_ms - samples[0].time_ms) / timeRange) * plotWidth;
    const barHeight = maximumQuantity === 0 ? 0 :
      (sample.quantity / maximumQuantity) * (height - bottom - quantityTop);
    context.fillRect(x, height - bottom - barHeight, barWidth, barHeight);
  }

  const labelCount = Math.min(5, samples.length);
  context.fillStyle = '#8f9bb3';
  for (let label = 0; label < labelCount; ++label) {
    const index = labelCount === 1 ? 0 :
      Math.round((label / (labelCount - 1)) * (samples.length - 1));
    const sample = samples[index];
    const x = left + ((sample.time_ms - samples[0].time_ms) / timeRange) * plotWidth;
    context.textAlign = label === 0 ? 'left' : label === labelCount - 1 ? 'right' : 'center';
    context.fillText(sample.timestamp, x, height - 12);
  }
  context.textAlign = 'left';
}

function updateLevels(id, levels, side) {
  const body = document.getElementById(id);
  const maximum = Math.max(1, ...levels.map(level => level.quantity));
  body.replaceChildren(...levels.map(level => {
    const row = document.createElement('tr');
    row.className = side;
    const sideCell = document.createElement('td');
    const price = document.createElement('td');
    const quantity = document.createElement('td');
    sideCell.textContent = side === 'ask' ? 'ASK' : 'BID';
    price.textContent = (level.price / PRICE_SCALE).toFixed(2);
    quantity.textContent = level.quantity;
    const depth = (level.quantity / maximum) * 100;
    const color = side === 'ask' ? 'rgba(255,112,133,.18)' : 'rgba(85,214,190,.18)';
    row.style.background = `linear-gradient(to left, ${color} ${depth}%, transparent ${depth}%)`;
    row.append(sideCell, price, quantity);
    return row;
  }));
}

async function update() {
  try {
    const response = await fetch('/data?after=' + samples.length, {cache: 'no-store'});
    const data = await response.json();
    if (data.start !== samples.length) samples.length = 0;
    samples.push(...data.samples);
    updateLevels('asks', data.asks.slice().reverse(), 'ask');
    updateLevels('bids', data.bids, 'bid');
    if (samples.length) {
      const latest = samples[samples.length - 1];
      document.getElementById('price').textContent =
        (latest.close / PRICE_SCALE).toFixed(2);
      document.getElementById('quantity').textContent = latest.quantity;
      document.getElementById('status').textContent = samples.length +
        ' samples · first point retained · latest market time ' + latest.timestamp;
      draw();
    }
  } catch (_) {
    document.getElementById('status').textContent = 'Waiting for the orderbook process…';
  }
}

window.addEventListener('resize', resize);
resize();
setInterval(update, 100);
update();
</script>
</body></html>)html";

int parseTimestampField(
    std::string_view timestamp,
    std::size_t offset,
    std::size_t length
) {
    int value{};
    const char* const begin = timestamp.data() + offset;
    const char* const end = begin + length;
    const auto [position, error] = std::from_chars(begin, end, value);
    if (error != std::errc{} || position != end) {
        throw std::invalid_argument("Invalid ClickHouse timestamp: " + std::string(timestamp));
    }
    return value;
}

std::int64_t timestampMilliseconds(std::string_view timestamp) {
    if (timestamp.size() < 23 || timestamp[4] != '-' || timestamp[7] != '-' ||
        timestamp[10] != ' ' || timestamp[13] != ':' || timestamp[16] != ':' ||
        timestamp[19] != '.') {
        throw std::invalid_argument("Invalid ClickHouse timestamp: " + std::string(timestamp));
    }

    const std::chrono::year_month_day date{
        std::chrono::year(parseTimestampField(timestamp, 0, 4)),
        std::chrono::month(static_cast<unsigned>(parseTimestampField(timestamp, 5, 2))),
        std::chrono::day(static_cast<unsigned>(parseTimestampField(timestamp, 8, 2)))
    };
    if (!date.ok()) {
        throw std::invalid_argument("Invalid ClickHouse timestamp: " + std::string(timestamp));
    }

    const auto dayMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::sys_days(date).time_since_epoch()
    ).count();
    const std::int64_t hour = parseTimestampField(timestamp, 11, 2);
    const std::int64_t minute = parseTimestampField(timestamp, 14, 2);
    const std::int64_t second = parseTimestampField(timestamp, 17, 2);
    const std::int64_t millisecond = parseTimestampField(timestamp, 20, 3);
    if (hour > 23 || minute > 59 || second > 59) {
        throw std::invalid_argument("Invalid ClickHouse timestamp: " + std::string(timestamp));
    }
    return dayMilliseconds + hour * 3'600'000 + minute * 60'000 +
        second * 1'000 + millisecond;
}

void sendAll(int socket, std::string_view content) {
    while (!content.empty()) {
#ifdef __APPLE__
        const ssize_t sent = ::send(socket, content.data(), content.size(), 0);
#else
        const ssize_t sent = ::send(socket, content.data(), content.size(), MSG_NOSIGNAL);
#endif
        if (sent <= 0) {
            return;
        }
        content.remove_prefix(static_cast<std::size_t>(sent));
    }
}

void sendResponse(int socket, std::string_view contentType, std::string_view body) {
    const std::string header =
        "HTTP/1.1 200 OK\r\nContent-Type: " + std::string(contentType) +
        "\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: " +
        std::to_string(body.size()) + "\r\n\r\n";
    sendAll(socket, header);
    sendAll(socket, body);
}

std::size_t requestedSample(std::string_view request) {
    constexpr std::string_view prefix = "GET /data?after=";
    if (!request.starts_with(prefix)) {
        return 0;
    }

    const std::size_t end = request.find(' ', prefix.size());
    const std::string_view text = request.substr(prefix.size(), end - prefix.size());
    std::size_t value{};
    const auto [position, error] = std::from_chars(
        text.data(),
        text.data() + text.size(),
        value
    );
    return error == std::errc{} && position == text.data() + text.size() ? value : 0;
}

void openBrowser(const std::string& url) {
#ifdef __APPLE__
    pid_t process{};
    char* arguments[] = {
        const_cast<char*>("/usr/bin/open"),
        const_cast<char*>(url.c_str()),
        nullptr
    };
    if (::posix_spawn(&process, arguments[0], nullptr, nullptr, arguments, environ) == 0) {
        static_cast<void>(::waitpid(process, nullptr, 0));
    }
#else
    static_cast<void>(url);
#endif
}

} // namespace

PriceGui::PriceGui(std::chrono::milliseconds interval, bool simulated)
    : interval_(interval),
      simulated_(simulated),
      listenSocket_(::socket(AF_INET, SOCK_STREAM, 0)) {
    if (interval_.count() <= 0) {
        throw std::invalid_argument("Price GUI interval must be positive");
    }
    if (listenSocket_ < 0) {
        throw std::runtime_error("Could not create the price GUI server");
    }

    int enabled = 1;
    static_cast<void>(::setsockopt(
        listenSocket_, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)
    ));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(listenSocket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        ::listen(listenSocket_, 8) != 0) {
        ::close(listenSocket_);
        throw std::runtime_error("Could not start the price GUI server");
    }

    socklen_t addressSize = sizeof(address);
    if (::getsockname(
            listenSocket_, reinterpret_cast<sockaddr*>(&address), &addressSize
        ) != 0) {
        ::close(listenSocket_);
        throw std::runtime_error("Could not read the price GUI address");
    }

    url_ = "http://127.0.0.1:" + std::to_string(ntohs(address.sin_port));
    server_ = std::thread(&PriceGui::serverLoop, this);
    std::thread(openBrowser, url_).detach();
}

PriceGui::~PriceGui() {
    finish();
}

void PriceGui::addTrade(const Trade& trade, std::string_view timestamp) {
    std::lock_guard lock(mutex_);
    const std::int64_t marketMilliseconds = timestampMilliseconds(timestamp);
    const std::int64_t bucketMilliseconds =
        marketMilliseconds - marketMilliseconds % interval_.count();
    if (pendingBucketMilliseconds_.has_value() &&
        bucketMilliseconds != *pendingBucketMilliseconds_) {
        flushPendingSample();
    }
    pendingBucketMilliseconds_ = bucketMilliseconds;
    pendingClosingPrice_ = trade.price;
    pendingTimestamp_ = timestamp;
    if (trade.quantity > std::numeric_limits<Order::Quantity>::max() - pendingQuantity_) {
        pendingQuantity_ = std::numeric_limits<Order::Quantity>::max();
    } else {
        pendingQuantity_ += trade.quantity;
    }
}

void PriceGui::flushPendingSample() {
    if (!pendingBucketMilliseconds_.has_value() ||
        !pendingClosingPrice_.has_value() || !pendingTimestamp_.has_value()) {
        return;
    }
    samples_.push_back({
        *pendingBucketMilliseconds_,
        *pendingTimestamp_,
        *pendingClosingPrice_,
        pendingQuantity_
    });
    pendingBucketMilliseconds_.reset();
    pendingClosingPrice_.reset();
    pendingTimestamp_.reset();
    pendingQuantity_ = 0;
}

void PriceGui::completeReplay() {
    std::lock_guard lock(mutex_);
    flushPendingSample();
}

void PriceGui::updateOrderBook(const OrderBook& orderBook) {
    std::vector<BookLevel> bids;
    std::vector<BookLevel> asks;
    bids.reserve(5);
    asks.reserve(5);

    const auto& bidLevels = orderBook.getBidPriceLevels();
    for (auto position = bidLevels.rbegin();
         position != bidLevels.rend() && bids.size() < 5;
         ++position) {
        const Order::Quantity quantity = position->second.totalQuantity();
        if (quantity > 0) {
            bids.push_back({position->first, quantity});
        }
    }

    const auto& askLevels = orderBook.getAskPriceLevels();
    for (auto position = askLevels.begin();
         position != askLevels.end() && asks.size() < 5;
         ++position) {
        const Order::Quantity quantity = position->second.totalQuantity();
        if (quantity > 0) {
            asks.push_back({position->first, quantity});
        }
    }

    std::lock_guard lock(mutex_);
    bids_ = std::move(bids);
    asks_ = std::move(asks);
}

void PriceGui::finish() {
    if (stopped_) {
        return;
    }
    {
        std::lock_guard lock(mutex_);
        flushPendingSample();
        stopRequested_ = true;
    }
    ::shutdown(listenSocket_, SHUT_RDWR);
    ::close(listenSocket_);
    server_.join();
    stopped_ = true;
}

const std::string& PriceGui::url() const noexcept {
    return url_;
}

void PriceGui::serverLoop() {
    while (true) {
        const int client = ::accept(listenSocket_, nullptr, nullptr);
        if (client < 0) {
            std::lock_guard lock(mutex_);
            if (stopRequested_) {
                return;
            }
            continue;
        }

#ifdef __APPLE__
        int enabled = 1;
        static_cast<void>(::setsockopt(
            client, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)
        ));
#endif
        timeval timeout{.tv_sec = 1, .tv_usec = 0};
        static_cast<void>(::setsockopt(
            client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)
        ));

        char buffer[4096]{};
        const ssize_t received = ::recv(client, buffer, sizeof(buffer) - 1, 0);
        if (received > 0) {
            const std::string_view request(buffer, static_cast<std::size_t>(received));
            if (request.starts_with("GET /data?after=")) {
                const std::string body = dataJson(requestedSample(request));
                sendResponse(client, "application/json", body);
            } else {
                if (simulated_) {
                    std::string simulated(page);
                    const auto replace = [&simulated](std::string_view from, std::string_view to) {
                        const auto at = simulated.find(from);
                        if (at != std::string::npos) simulated.replace(at, from.size(), to);
                    };
                    replace("OrderBook — all samples since start", "SIMULATED order book · 000001.SZ");
                    replace("<div class=\"cards\">", "<p>Randomly generated orders and trades · 22 simulated sessions · CNY. Not historical market data.</p><div class=\"cards\">");
                    replace("Latest close", "Latest simulated trade (CNY)");
                    replace("latest market time", "simulated time");
                    sendResponse(client, "text/html; charset=utf-8", simulated);
                } else {
                    sendResponse(client, "text/html; charset=utf-8", page);
                }
            }
        }
        ::close(client);
    }
}

std::string PriceGui::dataJson(std::size_t afterSample) {
    std::lock_guard lock(mutex_);
    const std::size_t start = std::min(afterSample, samples_.size());
    std::string json = "{\"start\":" + std::to_string(start) + ",\"samples\":[";
    for (std::size_t index = start; index < samples_.size(); ++index) {
        if (index != start) {
            json.push_back(',');
        }
        const Sample& sample = samples_[index];
        json += "{\"time_ms\":" + std::to_string(sample.timeMilliseconds) +
            ",\"timestamp\":\"" + sample.timestamp + "\"" +
            ",\"close\":" + std::to_string(sample.closingPrice) +
            ",\"quantity\":" + std::to_string(sample.quantity) + "}";
    }
    json += "],\"bids\":[";
    for (std::size_t index = 0; index < bids_.size(); ++index) {
        if (index != 0) {
            json.push_back(',');
        }
        json += "{\"price\":" + std::to_string(bids_[index].price) +
            ",\"quantity\":" + std::to_string(bids_[index].quantity) + "}";
    }
    json += "],\"asks\":[";
    for (std::size_t index = 0; index < asks_.size(); ++index) {
        if (index != 0) {
            json.push_back(',');
        }
        json += "{\"price\":" + std::to_string(asks_[index].price) +
            ",\"quantity\":" + std::to_string(asks_[index].quantity) + "}";
    }
    json += "]}";
    return json;
}
