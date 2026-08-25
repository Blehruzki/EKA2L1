import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;

import java.io.FileWriter;
import java.io.PrintWriter;

// Round 19 found the exact dispatch mechanism in drtrvct2 (classic RVCT switch
// byte-jump-table trampoline): r12 = *(lr-1) is the max valid case index, r3 is
// bounds-checked against it, then the byte at [lr+r3] (or [lr+r12] if out of range,
// the clamped default) becomes a *2 offset added to lr to get the branch target.
// lr=0x821b6195 (confirmed live, both func=31 and func=32 traces) is the table base
// in baksrv.dll. Dump the actual bytes: the max-index byte at lr-1, and the table
// entries at r3=0x49 (func=31) and r3=0x4d (func=32), plus a wider window to see
// the whole table and manually verify against the known landing addresses
// (0x821b6226 for func=31, 0x821b622e for func=32).
public class DumpSwitchTable extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/switch_table.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        long lr = 0x821b6195L;
        // lr has the Thumb bit set (odd address) - the real table base is lr-1
        // (even), same "-1" the trampoline itself uses for the max-index byte,
        // consistent with reading raw bytes (no Thumb-bit adjustment needed for
        // plain byte reads).
        Address tableBase = toAddr(lr - 1);

        out.println("table base (lr-1) = " + tableBase);

        byte[] buf = new byte[0x60];
        int n = currentProgram.getMemory().getBytes(tableBase, buf);
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < n; i++) {
            if (i % 16 == 0) sb.append(String.format("%n%s (idx -1+%d=0x%x): ", tableBase.add(i), i, i - 1));
            sb.append(String.format("%02x ", buf[i] & 0xff));
        }
        out.println("raw table bytes:" + sb);

        int maxIndex = buf[0] & 0xff;
        out.println();
        out.printf("max valid index (byte at lr-1) = 0x%x (%d)%n", maxIndex, maxIndex);

        int[] checkIndices = { 0x49, 0x4d };
        for (int idx : checkIndices) {
            int entry = buf[1 + idx] & 0xff; // buf[0] is the -1 slot, so +1 offset
            long target = lr - 1 + (entry * 2L);
            out.printf("index 0x%x: in_range=%b, table_entry_byte=0x%x, computed_target=0x%x%n",
                idx, idx <= maxIndex, entry, target);
        }

        out.close();
        println("WROTE " + outPath);
    }
}
