import type { AppConfig } from '../types';
import type { DeepPartial } from '../defaults';

export interface PageProps {
  cfg: AppConfig;
  update: (patch: DeepPartial<AppConfig>) => void;
}
