import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DumpEuserTarget extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/dump_euser_target.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath, true));
        out.printf("=== %s ===%n", currentProgram.getName());

        Address target = toAddr(0x80399F48L);

        // Function containing / preceding
        Function f = getFunctionBefore(target);
        if (f != null) {
            out.printf("Nearest preceding function: %s @ %s%n", f.getName(), f.getEntryPoint());
        }

        // Cross references TO this address
        ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(target);
        int count = 0;
        while (refs.hasNext() && count < 20) {
            Reference r = refs.next();
            out.printf("  xref from %s (%s)%n", r.getFromAddress(), r.getReferenceType());
            count++;
        }
        if (count == 0) out.println("  (no xrefs found to this address)");

        // Disassemble surrounding range
        Listing listing = currentProgram.getListing();
        Address start = toAddr(0x80399F00L);
        Address end = toAddr(0x80399FB0L);
        Address a = start;
        while (a.compareTo(end) < 0) {
            Instruction ins = listing.getInstructionAt(a);
            if (ins == null) {
                try {
                    disassemble(a);
                    ins = listing.getInstructionAt(a);
                } catch (Exception e) {
                    out.printf("%s: disasm failed: %s%n", a, e.getMessage());
                    a = a.add(2);
                    continue;
                }
            }
            if (ins != null) {
                StringBuilder rawb = new StringBuilder();
                for (int i = 0; i < ins.getLength(); i++) {
                    rawb.append(String.format("%02x ", getByte(a.add(i)) & 0xff));
                }
                out.printf("%s  [%s] %s%n", a, rawb.toString().trim(), ins.toString());
                a = a.add(ins.getLength());
            } else {
                try {
                    byte b0 = getByte(a);
                    byte b1 = getByte(a.add(1));
                    out.printf("%s  .byte 0x%02x, 0x%02x%n", a, b0 & 0xff, b1 & 0xff);
                } catch (Exception e) {
                    out.printf("%s: read failed%n", a);
                }
                a = a.add(2);
            }
        }

        out.close();
        println("WROTE " + outPath);
    }
}
