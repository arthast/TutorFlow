-- Создание отдельной БД на сервис (PLAN §1, §4).
-- Запускается postgres-контейнером ОДИН раз при первой инициализации тома
-- (docker-entrypoint-initdb.d). Чтобы пересоздать — `docker compose down -v`.
--
-- Все БД владеет ролью POSTGRES_USER из .env (в MVP — одна роль `tutorflow`;
-- отдельные роли на сервис — задел на будущее, PLAN §1).
--
-- CREATE DATABASE нельзя выполнить условно/в транзакции, поэтому используем
-- идемпотентный приём через \gexec.

SELECT 'CREATE DATABASE identity_db'
 WHERE NOT EXISTS (SELECT FROM pg_database WHERE datname = 'identity_db')\gexec

SELECT 'CREATE DATABASE lesson_db'
 WHERE NOT EXISTS (SELECT FROM pg_database WHERE datname = 'lesson_db')\gexec

SELECT 'CREATE DATABASE assignment_db'
 WHERE NOT EXISTS (SELECT FROM pg_database WHERE datname = 'assignment_db')\gexec

SELECT 'CREATE DATABASE finance_db'
 WHERE NOT EXISTS (SELECT FROM pg_database WHERE datname = 'finance_db')\gexec

SELECT 'CREATE DATABASE file_db'
 WHERE NOT EXISTS (SELECT FROM pg_database WHERE datname = 'file_db')\gexec

SELECT 'CREATE DATABASE notification_db'
 WHERE NOT EXISTS (SELECT FROM pg_database WHERE datname = 'notification_db')\gexec

SELECT 'CREATE DATABASE report_db'
 WHERE NOT EXISTS (SELECT FROM pg_database WHERE datname = 'report_db')\gexec

SELECT 'CREATE DATABASE chat_db_shard0'
 WHERE NOT EXISTS (SELECT FROM pg_database WHERE datname = 'chat_db_shard0')\gexec

SELECT 'CREATE DATABASE chat_db_shard1'
 WHERE NOT EXISTS (SELECT FROM pg_database WHERE datname = 'chat_db_shard1')\gexec
