#pragma once

#include <od/objects/Object.h>

namespace scope_unit
{

  class Scope : public od::Object
  {
  public:
    Scope();
    virtual ~Scope();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mInL{"In L"};
    od::Inlet mInR{"In R"};
    od::Outlet mOutL{"Out L"};
    od::Outlet mOutR{"Out R"};
#endif
  };

} // namespace scope_unit
