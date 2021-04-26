#include <string>
#include <vector>
#include <cstddef>
#include <algorithm>
#include <numeric>
#include "num.hpp"

namespace encoder {
    using namespace std;

    typedef vector<byte> buf_t;
    typedef byte bytes32_t[32];
    static const size_t ETH_WORD_SIZE = 32;

    class EncodeBuffer {
    private:
        buf_t& _buf;
        size_t _pos;

    public:
        EncodeBuffer(buf_t& buf, size_t pos): _buf(buf), _pos(pos) {}
        size_t pos() const { return _pos; }
        const buf_t& buffer() const { return _buf; }
        void reserve(size_t needed) {
            _buf.resize(_pos + needed);
        }
        void write(const byte* start, const byte* end) {
            reserve(size_t(end - start));
            for (auto p = start; p != end; ++p) {
                _buf[_pos++] = *p;
            }
        }
        EncodeBuffer view(size_t pos) {
            if (pos > _pos) {
                reserve(pos - _pos);
            }
            return EncodeBuffer(_buf, pos);
        }
    };

    size_t align_size(size_t s) {
        if (s % ETH_WORD_SIZE) {
            return s + (ETH_WORD_SIZE - (s % ETH_WORD_SIZE));
        }
        return s;
    }

    void write_aligned_bytes(
        EncodeBuffer& buf,
        const byte* start,
        const byte* end
    ) {
        static const byte* filler_bytes = (const byte*)
            "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
            "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00";
        auto size = size_t(end) - size_t(start);
        auto aligned_size = align_size(size);
        auto fill_size = aligned_size - size;
        buf.write(start, end);
        buf.write(
            (const byte*) &filler_bytes,
            (const byte*) &filler_bytes + fill_size
        );
    }

    template <class TIterator>
    void write_aligned_bytes(
        EncodeBuffer& buf,
        TIterator start,
        TIterator end
    ) {
        return write_aligned_bytes(buf, &*start, &*end);
    }

    template <class TIntType>
    void write_word(EncodeBuffer& buf, TIntType n) {
        auto w = uint256_t(n);
        byte word_bytes[ETH_WORD_SIZE];
        for (size_t i = 0; i < ETH_WORD_SIZE; ++i) {
            word_bytes[ETH_WORD_SIZE - i - 1] = byte(unsigned(w & 0xFF));
            w = w >> 0xFF;
        }
        assert(w == 0);
        buf.write((const byte*) &word_bytes, (const byte*) &word_bytes + ETH_WORD_SIZE);
    }

    namespace values {
        class DataValue {
        public:
            virtual size_t encoded_size() const = 0;
            virtual void encode_to(EncodeBuffer& buf, size_t prefix_size = 0) const = 0;
        };

        template <class TValue>
        class NumericValue: public DataValue {
        private:
            TValue _v;

        public:
            NumericValue(const TValue& v): _v(v) {}
            size_t encoded_size() const override { return ETH_WORD_SIZE; };
            void encode_to(EncodeBuffer& buf) const override {
                write_word(_v, buf);
            }
        };

        typedef NumericValue<uint256_t> Uint256Value;
        typedef NumericValue<int256_t> Int256Value;
        typedef NumericValue<bytes32_t> Bytes32Value;

        class BytesArrayValue: public DataValue {
        private:
            buf_t _bytes;

        public:
            BytesArrayValue(const buf_t& v): _bytes(v) {}
            size_t encoded_size() const override {
                return ETH_WORD_SIZE + align_size(_bytes.size());
            }
            void encode_to(EncodeBuffer& buf, size_t) const override {
                write_word(buf, _bytes.size());
                write_aligned_bytes(buf, _bytes.cbegin(), _bytes.cend());
            }
        };

        class RefListValue: DataValue {
        protected:
            vector<DataValue*> _elements;

            virtual size_t encoded_array_size() const {
                return _elements.size() * ETH_WORD_SIZE;
            }

        public:
            RefListValue(const vector<DataValue*>& elements):
                _elements(elements) {}
            size_t length() const { return _elements.size(); }
            size_t encoded_size() const override {
                auto total_size = encoded_array_size();
                // Data for each element will be appended at the end of the array.
                for (auto i = _elements.cbegin(); i != _elements.cend(); ++i) {
                    total_size += (*i)->encoded_size();
                }
                return total_size;
            }
            void encode_to(EncodeBuffer& buf, size_t prefix_size = 0) const override {
                // Prepare a bufffer at the end of the array for element data.
                size_t data_start = buf.pos() + encoded_array_size();
                auto data_buf = buf.view(data_start);
                size_t head_pos = buf.pos() - prefix_size;
                // Write elements.
                for (size_t i = 0; i < _elements.size(); ++i) {
                    auto e = _elements[i];
                    // Write offset to element data.
                    write_word(buf, data_buf.pos() - head_pos);
                    // Write element data.
                    e->encode_to(data_buf);
                }
            }
        };

        template <class TElementValue, typename TBase=RefListValue>
        class HomogeneousRefListValue: public TBase {
        public:
            HomogeneousRefListValue(const vector<TElementValue*>& elements)
                : TBase(elements) {}
            size_t encoded_size() const override {
                auto total_size = TBase::encoded_array_size();
                // Data for each element will be appended at the end of the array.
                if (TBase::_elements.size()) {
                    // No need to check all elements because the list is homogeneous.
                    total_size += TBase::_elements.size()
                        * TBase::_elements[0]->encoded_size();
                }
                return total_size;
            }
        };

        class InlineListValue : public DataValue {
        protected:
            vector<DataValue*> _elements;

            virtual size_t encoded_array_size() const {
                size_t total_size = 0;
                // Inline element values inside the array.
                for (auto i = _elements.cbegin(); i != _elements.cend(); ++i) {
                    total_size += (*i)->encoded_size();
                }
                return total_size;
            }

        public:
            InlineListValue(const vector<DataValue*>& elements)
                : _elements(elements) {}
            size_t length() const { return _elements.size(); }
            size_t encoded_size() const override {
                // All data is inside the array.
                return encoded_array_size();
            }
            void encode_to(EncodeBuffer& buf, size_t) const override {
                for (auto i = _elements.cbegin(); i != _elements.cend(); ++i) {
                    // Inline element data.
                    (*i)->encode_to(buf);
                }
            }
        };

        template <
            class TElementValue,
            typename TBase=InlineListValue
        >
        class HomogeneousInlineListValue : public TBase {
        protected:
            size_t encoded_array_size() const override {
                // Inline element values inside the array.
                if (TBase::_elements.size()) {
                    // No need to check each element because the list is
                    // homogeneous.
                    return TBase::_elements[0]->encoded_size()
                        * TBase::_elements.size();
                }
                return 0;
            }

        public:
            HomogeneousInlineListValue(const vector<TElementValue*>& elements)
                : TBase(elements) {}
        };

        template <
            class TElementValue,
            typename TBase=HomogeneousRefListValue<TElementValue>
        >
        class DynamicRefArrayValue: public TBase {
        public:
            DynamicRefArrayValue(const vector<TElementValue*>& v): TBase(v) {}
            size_t encoded_size() const override {
                return TBase::encoded_size() + ETH_WORD_SIZE;
            }
            void encode_to(EncodeBuffer& buf, size_t) const override {
                write_word(buf, TBase::length());
                TBase::encode_to(buf, ETH_WORD_SIZE);
            }
        };

        template <
            class TElementValue,
            typename TBase=HomogeneousInlineListValue<TElementValue>
        >
        class DynamicInlineArrayValue: public TBase {
        public:
            DynamicInlineArrayValue(const vector<TElementValue*>& v): TBase(v) {}
            size_t encoded_size() const override {
                return TBase::encoded_size() + ETH_WORD_SIZE;
            }
            void encode_to(EncodeBuffer& buf, size_t) const override {
                write_word(buf, TBase::length());
                TBase::encode_to(buf, ETH_WORD_SIZE);
            }
        };

        template <class TElementValue>
        using FixedRefArrayValue = HomogeneousRefListValue<TElementValue>;

        template <class TElementValue>
        using FixedInlineArrayValue = HomogeneousInlineListValue<TElementValue>;

        // Efficient version of DynamicInlineArrayValue for numeric elements only.
        template <class TNumeric>
        class DynamicNumericArrayValue: public DataValue {
        private:
            vector<TNumeric> _numbers;
        public:
            DynamicNumericArrayValue(const vector<TNumeric>& numbers)
                : _numbers(numbers) {}
            size_t encoded_size() const override {
                return (_numbers.size() + 1) * ETH_WORD_SIZE;
            }
            void encode_to(EncodeBuffer& buf, size_t) const override {
                write_word(buf, _numbers.size());
                for (auto i = _numbers.cbegin(); i != _numbers.cend(); ++i) {
                    write_word(buf, *i);
                }
            }
        };

        // Efficient version of FixedInlineArrayValue for numeric elements only.
        template <class TNumeric>
        class FixedNumericArrayValue: public DataValue {
        private:
            vector<TNumeric> _numbers;
        public:
            FixedNumericArrayValue(const vector<TNumeric>& numbers)
                : _numbers(numbers) {}
            size_t encoded_size() const override {
                return _numbers.size() * ETH_WORD_SIZE;
            }
            void encode_to(EncodeBuffer& buf, size_t) const override {
                for (auto i = _numbers.cbegin(); i != _numbers.cend(); ++i) {
                    write_word(buf, *i);
                }
            }
        };

        typedef RefListValue RefStructValue;
        typedef InlineListValue InlineStructValue;
    }
}
