import { DataType, DbAccess } from '../storage/database.ts';
import checkPrinter from './checkPrinter';
import { CheckPrinterStatus } from './types';

const clearPrinter = (store: DbAccess) => async (): Promise<CheckPrinterStatus> => {
  const allDumps = (await store.getAll())
    .filter(({ type }) => type === DataType.RAW);

  for (const { timestamp } of allDumps) {
    await store.delete(timestamp);
  }

  return checkPrinter(store)()
};

export default clearPrinter;
