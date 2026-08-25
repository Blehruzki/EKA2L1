import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.listing.ProgramContext;
import ghidra.util.task.ConsoleTaskMonitor;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.math.BigInteger;

// Round 31 found FUN_83485d62 (cdlserver.exe's merge-sort) is correctly bounded, but
// zero memory growth over 25 minutes means the stall is likely inside the comparator
// it calls per merge step, thunk_EXT_FUN_803a909a - an import into euser.dll.
// Disassemble it directly to see what it actually does with the two 4-byte record
// fields it's given.
public class DumpEuserComparator extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/euser_comparator.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        ProgramContext ctx = currentProgram.getProgramContext();
        Register tmodeReg = ctx.getRegister("TMode");
        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();

        Address dumpStart = toAddr(0x803a1380L);
        Address dumpEnd = toAddr(0x803a1420L);

        out.println("=== raw bytes 0x803a1380-0x803a1420 ===");
        Address a = dumpStart;
        while (a.compareTo(dumpEnd) <= 0) {
            byte[] buf = new byte[16];
            int n = currentProgram.getMemory().getBytes(a, buf);
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < n; i++) sb.append(String.format("%02x ", buf[i]));
            out.printf("%s: %s%n", a, sb.toString());
            a = a.add(16);
        }

        out.println();
        out.println("=== try Thumb disasm at 0x803a139e ===");
        Address t = toAddr(0x803a139eL);
        clearListing(t, toAddr(0x803a1420L));
        if (tmodeReg != null) {
            ctx.setValue(tmodeReg, t, toAddr(0x803a1420L), BigInteger.ONE);
        }
        disassemble(t);
        InstructionIterator ins2 = listing.getInstructions(t, true);
        int c2 = 0;
        while (ins2.hasNext() && c2 < 60) {
            Instruction i = ins2.next();
            if (i.getAddress().compareTo(toAddr(0x803a1420L)) > 0) break;
            out.printf("%s: %s%n", i.getAddress(), i.toString());
            c2++;
        }

        out.println();
        out.println("=== try ARM disasm at 0x803a139e ===");
        clearListing(t, toAddr(0x803a1420L));
        if (tmodeReg != null) {
            ctx.setValue(tmodeReg, t, toAddr(0x803a1420L), BigInteger.ZERO);
        }
        disassemble(t);
        InstructionIterator ins3 = listing.getInstructions(t, true);
        int c3 = 0;
        while (ins3.hasNext() && c3 < 60) {
            Instruction i = ins3.next();
            if (i.getAddress().compareTo(toAddr(0x803a1420L)) > 0) break;
            out.printf("%s: %s%n", i.getAddress(), i.toString());
            c3++;
        }

        out.close();
        println("WROTE " + outPath);
    }
}
