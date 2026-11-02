# Проверка очереди обложки Spotify

Запуск: python3 tests/spotify-artwork-plan-native.py [--compiler g++].
Нужны g++ и Python с loguru. Проверяется реальный production header с ASAN/UBSAN: origin, ограниченная длина, собственная копия URL, смена A/B/A, logout, дубли, задержки и переполнение таймера.

Компонент пока не связан с worker и LCD. Он не загружает изображения, не выделяет память и не вызывает LVGL. RequestPlan требует одного владельца или внешней блокировки; Request владеет копией URL. Сетевой transport и cache буферы будут отдельным этапом.
