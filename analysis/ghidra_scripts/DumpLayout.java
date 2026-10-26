// Print entry points and memory blocks so they can be compared with
// reports/XBE_HEADER.md (analysis/xbe_header.py).
// @category Manhunt
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressIterator;
import ghidra.program.model.mem.MemoryBlock;

public class DumpLayout extends GhidraScript {
    @Override
    protected void run() throws Exception {
        AddressIterator it = currentProgram.getSymbolTable().getExternalEntryPointIterator();
        while (it.hasNext()) {
            Address a = it.next();
            println("ENTRY " + a);
        }
        println("IMAGEBASE " + currentProgram.getImageBase());
        for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
            println(String.format("BLOCK %-12s %s size=0x%x %s%s%s",
                b.getName(), b.getStart(), b.getSize(),
                b.isRead() ? "r" : "-", b.isWrite() ? "w" : "-", b.isExecute() ? "x" : "-"));
        }
    }
}
