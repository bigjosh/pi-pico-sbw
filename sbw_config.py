from micropython import const


REGRESSION_DESCRIPTOR_ADDR_0 = const(0x1A00)
REGRESSION_DESCRIPTOR_EXPECTED_0 = const(0x0606)
REGRESSION_DESCRIPTOR_ADDR_1 = const(0x1A14)
REGRESSION_DESCRIPTOR_EXPECTED_1 = const(0x0811)
REGRESSION_RAM_ADDR_0 = const(0x2000)
REGRESSION_RAM_VALUE_0 = const(0x1234)
REGRESSION_RAM_ADDR_1 = const(0x2002)
REGRESSION_RAM_VALUE_1 = const(0xA55A)
REGRESSION_FRAM_ADDR = const(0xC400)
REGRESSION_FRAM_VALUE = const(0x1234)


def bytes_to_words_le(data):
    words = []
    for index in range(0, len(data), 2):
        words.append(data[index] | (data[index + 1] << 8))
    return tuple(words)
