"""Bank-aware control-flow discovery for a Game Boy ROM.

Phase 1 finds code by recursive descent from known entry points (reset,
RST/interrupt vectors, configured seeds, addresses logged by the runtime's
fallback interpreter), following jumps, calls and RST 08h jump tables, and
tracking the ROM bank selected by "ld a,N / rst 10h" style bank switches.

Phase 2 groups the discovered code into functions: each entry point owns
everything reachable from it by fallthrough and jumps, stopping at other
entry points.

Mistakes here never affect correctness, only speed: each function is a
faithful translation of the bytes at its (bank, address), the runtime
selects code by the bank actually mapped, and anything not translated
falls back to the interpreter.
"""

from dataclasses import dataclass, field
import sm83

FLOW_END = {"ret", "reti", "jphl", "illegal"}


@dataclass
class Function:
    bank: int
    entry: int
    insns: list = field(default_factory=list)


def region_bank(addr, bank):
    return 0 if addr < 0x4000 else bank


class Rom:
    def __init__(self, data):
        self.data = data
        self.banks = len(data) // 0x4000

    def fetcher(self, bank):
        data = self.data

        def f(a):
            if a < 0x4000:
                return data[a]
            if a < 0x8000:
                return data[bank * 0x4000 + (a - 0x4000)]
            return 0xFF
        return f

    def decode(self, bank, addr):
        return sm83.decode(self.fetcher(bank), addr)

    def word(self, bank, addr):
        f = self.fetcher(bank)
        return f(addr) | (f(addr + 1) << 8)


class Analyzer:
    def __init__(self, rom, cfg):
        self.rom = rom
        self.cfg = cfg
        self.insns = {}        # (bank, addr) -> Insn
        self.entries = set()   # (bank, addr)
        self.noreturn = set()  # (bank, addr) of routines that never return to the caller
        for s in cfg.get("noreturn", []):
            self.noreturn.add(parse_ref(s))
        self.jump_table_fns = cfg.get("jump_table_routines", [])
        self.bank_switch = cfg.get("bank_switch_routines", [])
        self.tables = []       # (bank, table addr, [targets])
        self.work = []

    def valid(self, bank, addr):
        if addr >= 0x8000:
            return False
        if addr >= 0x4000 and not (1 <= bank < self.rom.banks):
            return False
        return True

    def add_entry(self, bank, addr, state=None):
        bank = region_bank(addr, bank)
        if not self.valid(bank, addr):
            return
        self.entries.add((bank, addr))
        self.work.append((bank, addr, state))

    def run(self, seeds):
        for b, a in seeds:
            self.add_entry(b, a)
        if seeds:
            self.far_tables()
        while self.work:
            bank, addr, state = self.work.pop()
            self.trace(bank, addr, state)

    def callee_bank(self, cur_region_bank, known_bank, target):
        """Bank to assume for a call/jump target, or None if unknown."""
        if target < 0x4000:
            return 0
        if cur_region_bank:      # executing in switchable bank: same bank
            return known_bank if known_bank is not None else cur_region_bank
        return known_bank

    def trace(self, bank, addr, state):
        # state = (known A value or None, known mapped bank or None)
        a_val, mapped = state if state else (None, None)
        if bank:
            mapped = bank
        recent = []
        while True:
            key = (bank, addr)
            if not self.valid(bank, addr) or key in self.insns:
                return
            insn = self.rom.decode(bank, addr)
            self.insns[key] = insn
            op = insn.op
            k = op.kind
            recent = (recent + [insn])[-10:]

            # constant tracking for bank switches
            if op.opcode == 0x3E:
                a_val = insn.n8
            elif op.opcode == 0xAF:
                a_val = 0
            elif op.opcode == 0xEA and 0x2000 <= insn.n16 < 0x4000 and a_val is not None:
                mapped = (a_val & 0x1F) or 1
            elif op.sets_a:
                a_val = None

            nxt = insn.next
            # ld rr,ADDR / push rr: a pushed return address
            if (op.opcode in (0xC5, 0xD5, 0xE5) and len(recent) >= 2
                    and recent[-2].op.opcode == op.opcode - 0xC4):
                t = recent[-2].n16
                tb = self.callee_bank(bank, mapped, t)
                if tb is not None and 0x0100 <= t < 0x8000:
                    self.add_entry(tb, t)
            if k == "jphl":
                self.inline_table(bank, recent, mapped)
            if k in FLOW_END and not op.cond:
                return
            if k in ("jp", "jr"):
                t = insn.jump_target
                tb = self.callee_bank(bank, mapped, t)
                if tb is not None and region_bank(t, tb) == bank:
                    self.work.append((bank, t, (a_val, mapped)))
                elif tb is not None:
                    self.add_entry(tb, t, (a_val, mapped))
                if not op.cond:
                    return
            elif k in ("call", "rst"):
                t = insn.jump_target
                if t in self.bank_switch and a_val is not None:
                    mapped = (a_val & 0x1F) or 1
                tb = self.callee_bank(bank, mapped, t)
                if tb is not None:
                    self.add_entry(tb, t, (a_val, mapped))
                if t in self.jump_table_fns and not op.cond:
                    self.jump_table(bank, nxt, mapped)
                    return
                if tb is not None and (region_bank(t, tb), t) in self.noreturn:
                    return
                a_val = None  # callee may clobber A
            if region_bank(nxt, bank) != bank:
                # fell off the end of the fixed bank into the switchable one
                return
            addr = nxt

    def jump_table(self, bank, table, mapped):
        """Inline word table following an RST jump-table call."""
        targets = []
        lowest = 0x10000
        a = table
        for _ in range(128):
            if a >= lowest or region_bank(a + 1, bank) != bank:
                break
            w = self.rom.word(bank, a)
            tb = self.callee_bank(bank, mapped, w)
            if w >= 0x8000 or w < 0x0100 or tb is None or not self.valid(region_bank(w, tb), w):
                break
            first = self.rom.decode(region_bank(w, tb), w)
            if first.op.kind == "illegal" or first.raw[0] == 0xFF:
                break
            targets.append((region_bank(w, tb), w))
            if region_bank(w, tb) == bank and w > table:
                lowest = min(lowest, w)
            a += 2
        for tb, w in targets:
            self.add_entry(tb, w)
        self.tables.append((bank, table, targets))

    def inline_table(self, bank, recent, mapped):
        """ld hl,TABLE / add hl,rr / ld a,(hl+) / ld h,(hl) / ld l,a / jp hl"""
        ops = [i.op.opcode for i in recent]
        if ops[-4:] != [0x2A, 0x66, 0x6F, 0xE9]:
            return
        for i in reversed(recent[:-4]):
            if i.op.opcode in (0x09, 0x19):          # add hl,bc / add hl,de
                continue
            if i.op.opcode == 0x21:                  # ld hl,n16
                self.jump_table(bank, i.n16, mapped)
            return

    def far_tables(self):
        """Configured tables of 3-byte (bank, address) entries."""
        for t in self.cfg.get("far_tables", []):
            a = int(t["addr"], 16)
            for n in range(t.get("max", 256)):
                e = a + 3 * n
                b, w = self.rom.fetcher(0)(e), self.rom.word(0, e + 1)
                if not (1 <= b < self.rom.banks and 0x4000 <= w < 0x8000):
                    break
                self.add_entry(b, w)

    def no_return_after(self, bank, insn):
        t = insn.jump_target
        if insn.op.cond:
            return False
        if t in self.jump_table_fns:
            return True
        return (region_bank(t, bank), t) in self.noreturn

    # ---------------------------------------------------------- phase 2

    def functions(self):
        funcs = []
        for (bank, entry) in sorted(self.entries):
            if (bank, entry) not in self.insns:
                continue
            seen = set()
            stack = [entry]
            while stack:
                a = stack.pop()
                if a in seen or (bank, a) not in self.insns:
                    continue
                if a != entry and (bank, a) in self.entries:
                    continue   # another function: leave via EXIT
                seen.add(a)
                insn = self.insns[(bank, a)]
                k = insn.op.kind
                if k in FLOW_END and not insn.op.cond:
                    continue
                if k in ("call", "rst") and self.no_return_after(bank, insn):
                    continue
                if k in ("jp", "jr"):
                    t = insn.jump_target
                    if region_bank(t, bank) == bank:
                        stack.append(t)
                    if not insn.op.cond:
                        continue
                stack.append(insn.next)
            fn = Function(bank, entry, [self.insns[(bank, a)] for a in sorted(seen)])
            funcs.append(fn)
        return funcs


def parse_ref(s):
    """'BB:AAAA' or 'AAAA' -> (bank, addr)."""
    if ":" in s:
        b, a = s.split(":")
        b, a = int(b, 16), int(a, 16)
    else:
        b, a = 0, int(s, 16)
    return region_bank(a, b), a
