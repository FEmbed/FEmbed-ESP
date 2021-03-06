#ifndef _CRC_H_
#define _CRC_H_
/**
 * CRC校验公共类
 */
#include <stdint.h>
#include <stdlib.h>

namespace FEmbed {

class CRCUtils {
public:
    typedef enum {
        CRC16_MODBUS
    } crc16_type;
    
    CRCUtils(uint32_t init = 0xEDB88320);
    uint32_t crc32(uint8_t *ptr, uint32_t len, uint32_t in = 0xffffffff);
    uint16_t crc16(uint8_t *ptr, uint16_t len, bool swap = false,
            crc16_type type = CRC16_MODBUS); //Modbus

    static CRCUtils *get() {
      static CRCUtils *INST = NULL;
      if(INST == NULL)
      {
          INST = new CRCUtils();
      }
      return INST;
    }
    
private:
    unsigned int crc32table[256];
};

}

#endif
