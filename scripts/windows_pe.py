"""PE checks shared by the native and legacy Windows packagers."""
import struct


def require_x64(data, dll):
    if len(data) < 64 or data[:2] != b'MZ':
        raise ValueError('Not a Windows executable')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if pe > len(data) - 26 or data[pe:pe + 4] != b'PE\0\0':
        raise ValueError('Invalid PE header')
    machine = struct.unpack_from('<H', data, pe + 4)[0]
    flags = struct.unpack_from('<H', data, pe + 22)[0]
    magic = struct.unpack_from('<H', data, pe + 24)[0]
    if machine != 0x8664 or magic != 0x20b or bool(flags & 0x2000) != dll:
        raise ValueError('Expected an x64 PE32+ image with the correct DLL/EXE type')


def require_system_dll_search(data):
    require_x64(data, False)
    try:
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        opt = pe + 24
        opt_size = struct.unpack_from('<H', data, pe + 20)[0]
        if opt_size < 200 or struct.unpack_from('<I', data, opt + 108)[0] < 11:
            raise ValueError('Missing viewer load-configuration directory')
        rva, size = struct.unpack_from('<II', data, opt + 112 + 10 * 8)
        sections = opt + opt_size
        offset = None
        for index in range(struct.unpack_from('<H', data, pe + 6)[0]):
            section = sections + index * 40
            address, raw_size, raw_offset = struct.unpack_from('<III', data, section + 12)
            if address <= rva and rva + 80 <= address + raw_size:
                offset = raw_offset + rva - address
                break
        if size < 80 or offset is None or offset + 80 > len(data):
            raise ValueError('Missing viewer load-configuration flags')
        declared_size = struct.unpack_from('<I', data, offset)[0]
        flags = struct.unpack_from('<H', data, offset + 78)[0]
        if declared_size < 80 or flags != 0x800:
            raise ValueError('Viewer static imports need system-only DLL search; use the pinned MSVC CI')
    except struct.error as error:
        raise ValueError('Truncated viewer load configuration') from error
