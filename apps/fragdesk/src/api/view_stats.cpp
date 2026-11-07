// FragDesk – Handy-App: Statistiken (wie page_stats.cpp)
#include "views.h"

namespace fdapi {

void viewStats(ViewCtx& c, Response& res) {
    (void)c;
    res.error(501, "Kommt gleich");
}

}  // namespace fdapi
