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

#include "vitoconnect_optolinkP300.h"

namespace esphome {
  namespace vitoconnect {

    static const char* TAG = "vitoconnect";

    inline uint8_t calcChecksum( uint8_t array[], uint8_t length ) {
      uint8_t sum = 0;
      for( uint8_t i = 1; i < length - 1;
           ++i ) { // start with second byte and end before checksum
        sum += array[ i ];
      }
      return sum;
    }

    inline bool checkChecksum( uint8_t array[], uint8_t length ) {
      return ( array[ length - 1 ] == calcChecksum( array, length ) );
    }

    OptolinkP300::OptolinkP300( uart::UARTDevice* uart )
        : Optolink( uart )
        , _state( UNDEF )
        , _lastMillis( 0 )
        , _write( false )
        , _rcvBuffer{ 0 }
        , _rcvBufferLen( 0 )
        , _rcvLen( 0 ) {}

    void OptolinkP300::begin() { _state = RESET; }

    void OptolinkP300::loop() {
      switch( _state ) {
        case RESET:
          _reset();
          break;
        case RESET_ACK:
          _resetAck();
          break;
        case INIT:
          _init();
          break;
        case INIT_ACK:
          _initAck();
          break;
        case IDLE:
          _idle();
          break;
        case SEND:
          _send();
          break;
        case SEND_ACK:
          _sentAck();
          break;
        case RECEIVE:
          _receive();
          break;
        case RECEIVE_ACK:
          _receiveAck();
          break;
        default:
          // begin() not called
          break;
      }
      if( _queue.size() > 0 &&
          millis() - _lastMillis >
            5000UL ) { // if no ACK is coming, reset connection
        _tryOnError( TIMEOUT );
        _state = RESET;
        _uart->flush();
      }
      // TODO(@bertmelis): move timeouts here, clear queue on timeout
    }

    void OptolinkP300::_reset() {
      // Set communication with Vitotronic to defined state = reset to KW
      // protocol
      const uint8_t buff[] = { 0x04 };
      _uart->write_array( buff, sizeof( buff ) );
      _lastMillis = millis();
      _state      = RESET_ACK;
    }

    void OptolinkP300::_resetAck() {
      if( _uart->read() == 0x05 ) {
        // received 0x05/enquiry: optolink has been reset
        _lastMillis = millis();
        _state      = INIT;
      } else {
        if( millis() - _lastMillis > 1000 ) { // try again every 0,5sec
          _state = RESET;
        }
      }
    }

    void OptolinkP300::_init() {
      const uint8_t buff[] = { 0x16, 0x00, 0x00 };
      _uart->write_array( buff, sizeof( buff ) );
      _lastMillis = millis();
      _state      = INIT_ACK;
    }

    void OptolinkP300::_initAck() {
      if( _uart->available() ) {
        if( _uart->read() == 0x06 ) {
          // ACK received, moving to next state
          _lastMillis = millis();
          _state      = IDLE;
        }
      }
    }

    void OptolinkP300::_idle() {
      // send INIT every 5 seconds to keep communication alive
      if( millis() - _lastMillis > 5 * 1000UL ) {
        _state = INIT;
      }
      if( _queue.size() > 0 ) {
        _state = SEND;
      }
    }

    void OptolinkP300::_send() {
      uint8_t     buff[ MAX_DP_LENGTH + 8 ];
      OptolinkDP* dp      = _queue.front();
      uint8_t     length  = dp->length;
      uint16_t    address = dp->address;
      if( dp->write ) {
        // type is WRITE, has length of 8 chars + length of value
        buff[ 0 ] = 0x41;
        buff[ 1 ] = 5 + length;
        buff[ 2 ] = 0x00;
        buff[ 3 ] = 0x02;
        buff[ 4 ] = ( address >> 8 ) & 0xFF;
        buff[ 5 ] = address & 0xFF;
        buff[ 6 ] = length;
        // add value to message
        memcpy( &buff[ 7 ], dp->data, length );
        buff[ 7 + length ] = calcChecksum( buff, 8 + length );
        _uart->write_array( buff, 8 + length );
        _rcvLen = 8; // Written payload is not returned, the return length is
                     // always 8 bytes long
      } else {
        // type is READ
        // has fixed length of 8 chars
        buff[ 0 ] = 0x41;
        buff[ 1 ] = 0x05;
        buff[ 2 ] = 0x00;
        buff[ 3 ] = 0x01;
        buff[ 4 ] = ( address >> 8 ) & 0xFF;
        buff[ 5 ] = address & 0xFF;
        buff[ 6 ] = length;
        buff[ 7 ] = calcChecksum( buff, 8 );
        _rcvLen   = 8 + length; // expected answer length is 8 + data length
        _uart->write_array( buff, 8 );
      }
      _rcvBufferLen = 0;
      _lastMillis   = millis();
      _state        = SEND_ACK;
    }

    void OptolinkP300::_sentAck() {
      if( _uart->available() ) {
        uint8_t buff = _uart->read();
        if( buff == 0x06 ) { // transmit successful, moving to next state
          _state = RECEIVE;
          return;
        } else if( buff == 0x15 ) { // transmit negatively acknowledged, return
                                    // to IDLE
          _tryOnError( NACK );
          _state = IDLE;
          return;
        }
      }
    }

    void OptolinkP300::_receive() {
      while( _uart->available() != 0 ) { // read complete RX buffer
        _rcvBuffer[ _rcvBufferLen ] = _uart->read();
        ++_rcvBufferLen;
        _lastMillis = millis();
      }
      if( _rcvBuffer[ 0 ] != 0x41 ) {
        // wait for start byte
        return;
      }
      // ESP_LOGD(TAG, "buffer fill: %02x", _rcvBuffer[_rcvBufferLen-1]);
      // ESP_LOGD(TAG, "buffer fill: %d", _rcvBufferLen);
      // ESP_LOGD(TAG, "buffer fill: %d", _rcvLen);
      if( _rcvBufferLen == _rcvLen ) { // message complete, check message
        if( _rcvBuffer[ 1 ] != ( _rcvLen - 3 ) ) { // check for message length
          _tryOnError( LENGTH );
          _state = RECEIVE_ACK;
          return;
        }
        if( _rcvBuffer[ 2 ] != 0x01 ) { // Vitotronic returns an error message
          _tryOnError( VITO_ERROR );
          _state = RECEIVE_ACK;
          return;
        }
        if( !checkChecksum( _rcvBuffer, _rcvLen ) ) { // checksum is wrong
          _tryOnError( CRC );
          _state = RECEIVE_ACK; // TODO(@bertmelis): should we return NACK?
          return;
        }
        OptolinkDP* dp = _queue.front();
        if( _rcvBuffer[ 3 ] == 0x01 ) {
          // message is from READ command, so returning read value
          _tryOnData( &_rcvBuffer[ 7 ], dp->length );
        } else if( _rcvBuffer[ 3 ] == 0x03 ) {
          // message is from WRITE command, so returning written value
          _tryOnData( dp->data, dp->length );
        } else {
          // should not be here
        }
        _state = RECEIVE_ACK;
        return;
      } else {
        // not yet complete
      }
    }

    void OptolinkP300::_receiveAck() {
      const uint8_t buff[] = { 0x06 };
      _uart->write_array( buff, sizeof( buff ) );
      _lastMillis = millis();
      _state      = IDLE;
    }

  } // namespace vitoconnect
} // namespace esphome
