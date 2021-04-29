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
        enum class DataValueKind {
            Uint256,
            Int256,
            Bytes32,
            Bytes,
            List,
        };

        class DataValue {
        public:
            virtual size_t encoded_size() const = 0;
            virtual void encode(EncodeBuffer& buf) const = 0;
        };

        class Uint256Value: public DataValue {
        protected:
            const uint256_t _value;

        public:
            Uint256Value(uint256_t value): _value(value) {}
            size_t encoded_size() const override { return ETH_WORD_SIZE; }
            void encode(EncodeBuffer& buf) const override {
                write_word(buf, _value);
            }
        };

        class Int256Value: public DataValue {
        protected:
            const int256_t _value;

        public:
            Int256Value(int256_t value): _value(value) {}
            size_t encoded_size() const override { return ETH_WORD_SIZE; }
            void encode(EncodeBuffer& buf) const override {
                write_word(buf, _value);
            }
        };

        class Bytes32Value: public DataValue {
        protected:
            const bytes32_t _value;

        public:
            Bytes32Value(bytes32_t value): _value(value) {}
            size_t encoded_size() const override { return ETH_WORD_SIZE; }
            void encode(EncodeBuffer& buf) const override {
                write_word(buf, _value);
            }
        };

        class BytesValue: public DataValue {
        protected:
            const buf_t _value;

        public:
            BytesValue(const buf_t& value): _value(value) {}
            size_t encoded_size() const override { return sizeof(buf_t); }
            void encode(EncodeBuffer& buf) const override {
                buf.write(&_value, &_value + sizeof(buf_t));
            }
        };

        class ListValue: public DataValue {
        protected:
            const vector<DataValue*> _value;

        public:
            ListValue(const vector<DataValue*>& value): _value(value) {}
            size_t encoded_size() const override { throw "ListValue cannot be encoded"; }
            void encode(EncodeBuffer&) const override {
                throw "ListValue cannot be encoded";
            }
        };

        class DataEncoder {
        public:
            virtual size_t encoded_size(DataValue& value) const = 0;
            virtual void encode_to(EncodeBuffer& buf, DataValue& value, size_t prefix_size = 0) const = 0;
        };

        template <class TValue>
        class WordEncoder: public DataEncoder {
        public:
            WordEncoder(const TValue& v): _v(v) {}
            size_t encoded_size(WordDataValue v) const override { return ETH_WORD_SIZE; };
            void encode_to(EncodeBuffer& buf, const DataValue& value, size_t) const override {
                value.encode(buf);
            }
        };

        typedef WordEncoder<uint256_t> Uint256Encoder;
        typedef WordEncoder<int256_t> Int256Encoder;
        typedef WordEncoder<bytes32_t> Bytes32Encoder;

        class BytesArrayEncoder: public DataEncoder {
        private:
            buf_t _bytes;

        public:
            BytesArrayEncoder(const buf_t& v): _bytes(v) {}
            size_t encoded_size() const override {
                return ETH_WORD_SIZE + align_size(_bytes.size());
            }
            void encode_to(EncodeBuffer& buf, size_t) const override {
                write_word(buf, _bytes.size());
                write_aligned_bytes(buf, _bytes.cbegin(), _bytes.cend());
            }
        };

        class RefListEncoder: DataEncoder {
        protected:
            vector<DataEncoder*> _elements;

            virtual size_t encoded_array_size() const {
                return _elements.size() * ETH_WORD_SIZE;
            }

        public:
            RefListEncoder(const vector<DataEncoder*>& elements):
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

        template <class TElementValue, typename TBase=RefListEncoder>
        class HomogeneousRefListEncoder: public TBase {
        public:
            HomogeneousRefListEncoder(const vector<TElementValue*>& elements)
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

        class InlineListEncoder : public DataEncoder {
        protected:
            vector<DataEncoder*> _elements;

            virtual size_t encoded_array_size() const {
                size_t total_size = 0;
                // Inline element values inside the array.
                for (auto i = _elements.cbegin(); i != _elements.cend(); ++i) {
                    total_size += (*i)->encoded_size();
                }
                return total_size;
            }

        public:
            InlineListEncoder(const vector<DataEncoder*>& elements)
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
            typename TBase=InlineListEncoder
        >
        class HomogeneousInlineListEncoder : public TBase {
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
            HomogeneousInlineListEncoder(const vector<TElementValue*>& elements)
                : TBase(elements) {}
        };

        template <
            class TElementValue,
            typename TBase=HomogeneousRefListEncoder<TElementValue>
        >
        class DynamicRefArrayEncoder: public TBase {
        public:
            DynamicRefArrayEncoder(const vector<TElementValue*>& v): TBase(v) {}
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
            typename TBase=HomogeneousInlineListEncoder<TElementValue>
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
        using FixedRefArrayEncoder = HomogeneousRefListEncoder<TElementValue>;

        template <class TElementValue>
        using FixedInlineArrayEncoder = HomogeneousInlineListEncoder<TElementValue>;

        // Efficient version of DynamicInlineArrayValue for numeric elements only.
        template <class TNumeric>
        class DynamicNumericArrayEncoder: public DataEncoder {
        private:
            vector<TNumeric> _numbers;
        public:
            DynamicNumericArrayEncoder(const vector<TNumeric>& numbers)
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

        // Efficient version of FixedInlineArrayEncoder for numeric elements only.
        template <class TNumeric>
        class FixedNumericArrayEncoder: public DataEncoder {
        private:
            vector<TNumeric> _numbers;
        public:
            FixedNumericArrayEncoder(const vector<TNumeric>& numbers)
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

        typedef RefListEncoder RefStructEncoder;
        typedef InlineListEncoder InlineStructEncoder;

        class AbiEncoderBuilder {
            DataEncoder* fromFunction(const FunctionDefinition* d) {
                auto selector = MethodEncoder.toSelector(d);
                auto areElementsInline = all_of(
                    d->inputs.cbegin(),
                    d->inputs.cend(),
                    [](i) { return _isInline(i); }
                );
                if (areElementsInline) {
                    return new MethodEncoder(
                        selector,
                        InlineListEncoder(fromList(d->inputs))
                    );
                }
                return new MethodEncoder(
                    selector,
                    MixedListEncoder(fromList(d->inputs))
                );
            }
            DataEncoder* fromList(const vector<TypeDefinition*>& d) {
                auto areElementsInline = all_of(
                    d.cbegin(),
                    d.cend(),
                    [](i) { return _isInline(i); }
                );
                vector<DataEncoder*> elements;
                if (areElementsInline) {
                    return new InlineListEncoder(fromDefinitions(d));
                }
                return new MixedListEncoder(fromDefinitions(d));
            }
            DataEncoder* fromDefinition(const vector<TypeDefinition*>& d) {
                if (!d->isArray) {
                    if (d->baseType == "uint") {
                        return new UintEncoder(d->typeSize);
                    } else if (d->baseType == "int") {
                        return new IntEncoder(d->typeSize);
                    } else if (d->baseType == "bytes") {
                        return new BytesEncoder(d->typeSize);
                    }
                    throw "Unsupported baseType";
                } else {
                    if (!d->arraySize) {
                        if (d->baseType == "uint"
                            || d->baseType == "int"
                            || d->baseType == "bytes") {
                            return new DynamicWordArrayEncoder(d->typeSize);
                        } else {
                            return new DynamicArrayEncoder(fromDefinitions());
                        }
                    }
                    if (d->baseType == "uint"
                        || d->baseType == "int"
                        || d->baseType == "bytes") {
                        return new FixedWordArrayEncoder(d->typeSize);
                    }
                }
                auto areElementsInline = all_of(
                    d.cbegin(),
                    d.cend(),
                    [](i) { return _isInline(i); }
                );
            }
        };
    }
}
