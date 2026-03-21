// Stub for plaits/user_data.h — avoids STM32 dependency
// UserData::ptr() returns NULL (uses built-in FM patches)
#ifndef PLAITS_USER_DATA_STUB_H_
#define PLAITS_USER_DATA_STUB_H_

#include "stmlib/stmlib.h"
#include <cstdio>
#include <cstdint>

#define PAGE_SIZE 0x800

inline void FLASH_Unlock() { }
inline void FLASH_ErasePage(uint32_t) { }
inline void FLASH_ProgramWord(uint32_t, uint32_t) { }

namespace plaits {

class UserData {
 public:
  enum { ADDRESS = 0x08007000, SIZE = 0x1000 };
  UserData() { }
  ~UserData() { }
  inline const uint8_t* ptr(int) const { return NULL; }
  inline bool Save(uint8_t*, int) { return false; }
};

}  // namespace plaits

#endif
