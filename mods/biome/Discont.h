#pragma once

#include <od/objects/Object.h>

namespace stolmine
{

  class Discont : public od::Object
  {
  public:
    Discont();
    virtual ~Discont();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mIn{"In"};
    od::Outlet mOut{"Out"};
    od::Parameter mAmount{"Amount", 1.0f};
    od::Parameter mMix{"Mix", 1.0f};
    od::Parameter mMode{"Mode", 0.0f};
#endif
  };

} // namespace stolmine
