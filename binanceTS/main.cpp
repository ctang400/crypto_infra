#include <core/trade/TradeMain.h>

struct TradeTypes {};

int main(int argc, char *argv[]) {
  return TradeMain<TradeTypes>::instance().main(argc, argv);
}