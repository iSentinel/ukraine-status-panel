if (!id(neptun_client).alerts_valid()) {
  return std::string("--");
}
const uint8_t level = id(neptun_client).region_alert_level(
    id(ui_selected_region_id));
const bool cached = id(neptun_client).alerts_stale()
    || !id(neptun_client).alerts_last_fetch_ok();
std::string value;
if (level == 2U) {
  value = id(ui_language) == 0 ? "Повітряна тривога"
      : id(ui_language) == 1 ? "Воздушная тревога"
                             : "Air alert";
} else if (level == 1U) {
  value = id(ui_language) == 0 ? "Повітряна загроза"
      : id(ui_language) == 1 ? "Воздушная угроза"
                             : "Air threat";
} else {
  value = id(ui_language) == 0 ? "Немає активної"
      : id(ui_language) == 1 ? "Нет активной" : "No active";
}
if (cached) {
  value += id(ui_language) == 0 ? " · кеш"
      : id(ui_language) == 1 ? " · кэш" : " · cached";
}

const auto counts = id(neptun_client).region_threat_counts(
    id(ui_selected_region_id));
if (id(neptun_client).valid() && counts.total > 0U) {
  value += "\n";
  unsigned shown = 0U;
  unsigned kinds = 0U;
  const auto append = [&](const char *name, uint16_t count) {
    if (count == 0U) return;
    ++kinds;
    if (shown >= 3U) return;
    if (shown > 0U) value += " ";
    value += name;
    value += usp_ui::unsigned_decimal(count);
    ++shown;
  };
  append(id(ui_language) == 2 ? "UAV" : "БПЛА", counts.uav);
  append("FPV", counts.fpv);
  append(id(ui_language) == 0 ? "РОЗВ"
      : id(ui_language) == 1 ? "РАЗВ" : "REC", counts.recon);
  append(id(ui_language) == 2 ? "MSL" : "РКТ", counts.missile);
  append("BAL", counts.ballistic);
  append("KAB", counts.kab);
  append("MiG", counts.mig31k);
  append("?", counts.unknown);
  if (kinds > shown) {
    value += " +";
    value += usp_ui::unsigned_decimal(kinds - shown);
  }
}
return value;