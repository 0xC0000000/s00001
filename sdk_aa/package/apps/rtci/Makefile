include $(TOPDIR)/rules.mk

PKG_NAME:=rtci
PKG_VERSION:=01.00

TARGET_CFLAGS += -g -Wall

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)

include $(INCLUDE_DIR)/package.mk

define Package/$(PKG_NAME)
   SECTION:=apps
   CATEGORY:=apps
   TITLE:=RTC Module operate interface
endef

define Package/$(PKG_NAME)/description
	RTC module (DS1302) operation interface
endef

define Package/$(PKG_NAME)/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/$(PKG_NAME) $(1)/usr/bin/
endef

define Build/Prepare
	rm -rf $(PKG_BUILD_DIR)
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR)/ \
	CC="$(TARGET_CC)" \
	CFLAGS="$(TARGET_CFLAGS)"
endef

$(eval $(call BuildPackage,$(PKG_NAME)))
