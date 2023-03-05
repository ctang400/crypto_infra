#include <core/link/SSLLink.h>
#include <core/trade/HTTPMarketSession.h>
#include <core/trade/TradeMain.h>
#include <core/trade/TradeTypes.h>
#include <core/utils/llhttp/llhttp.h>

#define MY_LOG(SEVERITY, MSG)                                                  \
  (TL_LOG((SEVERITY), "Market::" << __func__ << ": " << MSG))

template <typename TYPES> struct Market {
  using market_trade_mediator_type = MarketTradeMediator<TYPES>;
  using MarketSessionType = HTTPMarketSession<TYPES>;

public:
  explicit Market(market_trade_mediator_type &mediator) {}
  void connect() {}
  void disconnect() {}
  bool configure(const ConfigFileParser &config, const std::string &section);
  bool init();
  bool marketCreateOrder(Order *order, StatusChangeReason &reason) {
    return true;
  }
  bool marketCancelOrder(Order *order, StatusChangeReason &reason) {
    return true;
  }
  template <typename LISTENER> void addToListener(LISTENER *listener) {
    listener->addChannel(m_market_session, DispatcherBase::ON_ALL);
  }
  MarketSessionType *m_market_session;
  std::string m_base_url;
};

template <typename TYPES>
bool Market<TYPES>::configure(const ConfigFileParser &config,
                              const std::string &section) {
  m_base_url = config.get<std::string>(section, "base_url");
  return true;
}
template <typename TYPES> bool Market<TYPES>::init() {
  m_market_session = new MarketSessionType(
      new SSLLink, HTTPMarketSessionProcessor<TYPES>(this), m_base_url);
  m_market_session->connect();
  std::string request = "GET /api/v3/time HTTP/1.1\r\nHost: " + m_base_url +
                        "\r\nConnection: keepAlive\r\n\r\n";
  // std::cout << request << std::endl;
  // m_market_session->write(request.c_str(), request.size());
  // m_market_session->write(request.c_str(), request.size());
  return true;
}

struct TradeTypes : DefaultTradeTypes<TradeTypes, Market<TradeTypes>> {};

int main(int argc, char *argv[]) {
  return TradeMain<TradeTypes>::instance().main(argc, argv);
}