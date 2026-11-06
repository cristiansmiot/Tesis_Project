"""A production database failure must never create a disposable local database."""

import unittest
from unittest.mock import patch

from app.database import _create_engine_safe


class DatabaseFailfastTests(unittest.TestCase):
    def test_postgres_engine_error_does_not_fall_back_to_sqlite(self):
        with patch("app.database.get_database_url", return_value="postgresql://user:secret@host/db"):
            with patch("app.database.create_engine", side_effect=RuntimeError("unavailable")) as create:
                with self.assertRaises(RuntimeError):
                    _create_engine_safe()
                self.assertEqual(create.call_count, 1)


if __name__ == "__main__":
    unittest.main()
