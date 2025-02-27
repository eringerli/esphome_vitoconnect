/* VitoWiFi

Copyright 2019 Bert Melis

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#pragma once

#include <assert.h>
#include <functional>
#include <stdint.h>
#include <string.h> // for memcpy

namespace esphome {
  namespace vitoconnect {

    class Datapoint {

    public:
      Datapoint();
      virtual ~Datapoint();

      void     setAddress( uint16_t address ) { this->_address = address; };
      uint16_t getAddress() { return this->_address; };

      void    setLength( uint8_t length ) { this->_length = length; };
      uint8_t getLength() { return this->_length; };

      static void onData(
        std::function< void( uint8_t[], uint8_t, Datapoint* dp ) > callback );
      void onError( uint8_t, Datapoint* dp );

      virtual void encode( uint8_t* raw, uint8_t length, void* data );
      virtual void
      decode( uint8_t* data, uint8_t length, Datapoint* dp = nullptr );

    protected:
      uint16_t _address;
      uint8_t  _length;
      static std::function< void( uint8_t[], uint8_t, Datapoint* dp ) >
        _stdOnData;
    };

  } // namespace vitoconnect
} // namespace esphome
