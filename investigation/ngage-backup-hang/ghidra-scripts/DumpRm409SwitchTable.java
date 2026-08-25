import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;

import java.io.FileWriter;
import java.io.PrintWriter;

// rm-409's baksrv.dll caller of the switch trampoline is at 0x80b847cc
// ("blx 0x80b85cc8"), so its table base (same "lr = return address, doubles as
// table pointer" idiom round 19 found) is the instruction right after: 0x80b847d0.
// Dump the table bytes and decode every valid case's target address, the same way
// round 19 hand-verified rm-320's table, to see whether this larger/fuller
// baksrv.dll build has real handlers where rm-320's was empty, or the same gap.
public class DumpRm409SwitchTable extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/rm409_switch_table.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        long lr = 0x80b847d0L;
        Address tableBase = toAddr(lr - 1);
        out.println("table base (lr-1) = " + tableBase);

        byte[] buf = new byte[0x80];
        int n = currentProgram.getMemory().getBytes(tableBase, buf);
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < n; i++) {
            if (i % 16 == 0) sb.append(String.format("%n%s (case %d): ", tableBase.add(i), i - 1));
            sb.append(String.format("%02x ", buf[i] & 0xff));
        }
        out.println("raw table bytes:" + sb);

        int maxIndex = buf[0] & 0xff;
        out.println();
        out.printf("max valid case index (byte at lr-1) = 0x%x (%d)%n", maxIndex, maxIndex);
        out.println();
        out.println("all case targets:");
        for (int idx = 0; idx <= maxIndex && (1 + idx) < n; idx++) {
            int entry = buf[1 + idx] & 0xff;
            long target = lr - 1 + (entry * 2L);
            out.printf("  case %d (0x%x): entry_byte=0x%x, target=0x%x%n", idx, idx, entry, target);
        }

        out.close();
        println("WROTE " + outPath);
    }
}
