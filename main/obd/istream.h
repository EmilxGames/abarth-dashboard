#pragma once

#include <cstddef>
#include <cstdint>

namespace abarth::obd {

/**
 * @brief Interfaccia generica per i flussi di dati verso il dongle OBD.
 */
class IStream {
public:
    virtual ~IStream() = default;

    /// Connette al dongle.
    virtual bool connect() = 0;

    /// Disconnette dal dongle.
    virtual void disconnect() = 0;

    /// Verifica se il dongle e' connesso.
    virtual bool isConnected() const = 0;

    /// Scrive dati grezzi verso il dongle.
    virtual size_t write(const uint8_t* data, size_t len) = 0;
    virtual size_t write(const char* str) = 0;

    /// Legge dati fino al terminatore o al timeout.
    virtual size_t readUntil(char terminator, char* out, size_t out_max, uint32_t timeout_ms) = 0;

    /// Svuota il buffer di ricezione.
    virtual void flushRx() = 0;
};

} // namespace abarth::obd
