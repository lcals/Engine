import { showcase_cases, ShowcaseCase } from "./showcase_cases.js";

export type LauncherChoice = ShowcaseCase;

export class LauncherModel {
    readonly choices = showcase_cases;
    selected: LauncherChoice | null = null;
    pending: LauncherChoice | "menu" | null = null;
    error: string | null = null;
    movie_requested = false;
    movie_playing = false;
    selection_count = 0;
}
