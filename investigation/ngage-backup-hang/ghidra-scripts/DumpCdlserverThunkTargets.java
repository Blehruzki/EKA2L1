import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;

import java.io.FileWriter;
import java.io.PrintWriter;

// Round 52 follow-up: 0x83486598 and 0x834865a0 are the real call targets from
// cdlserver.exe's own code (0x83485ff6 blx 0x83486598, 0x83486008 blx 0x834865a0).
// Both are still in cdlserver.exe's own address range, not drtaeabi.dll's
// (0x80394398-0x80397ce8) - almost certainly local import-thunk stubs (the same
// "thunk_EXT_FUN_*" pattern this investigation has repeatedly found for other
// module pairs). Dump their disassembly/bytes directly to find the real
// drtaeabi.dll target address each one jumps to.
public class DumpCdlserverThunkTargets extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/cdlserver_thunk_targets.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        long[] thunks = { 0x83486598L, 0x834865a0L };
        Listing listing = currentProgram.getListing();

        for (long t : thunks) {
            Address a = toAddr(t);
            out.printf("=== thunk @ %s ===%n", a);

            byte[] buf = new byte[16];
            try {
                currentProgram.getMemory().getBytes(a, buf);
                StringBuilder hex = new StringBuilder();
                for (byte b : buf) hex.append(String.format("%02x ", b));
                out.printf("raw bytes: %s%n", hex.toString());
            } catch (Exception e) {
                out.println("byte read failed: " + e.getMessage());
            }

            out.println("existing disassembly (if any):");
            InstructionIterator ins = listing.getInstructions(a, true);
            int count = 0;
            while (ins.hasNext() && count < 6) {
                Instruction i = ins.next();
                if (i.getAddress().getOffset() > t + 16) break;
                out.printf("%s: %s%n", i.getAddress(), i.toString());
                count++;
            }
            out.println();
        }

        out.close();
        println("WROTE " + outPath);
    }
}
